/*
  Bitcoin + Monero price ticker for ESP32-2432S028 (CYD, ST7789 version)
  v5: adds a weather page (triple-tap) based on the ZIP code entered during WiFi setup

  Libraries (Library Manager):
    - LovyanGFX
    - ArduinoJson (v7.x)
    - WiFiManager (by tzapu)

  Controls:
    - Tap once ........ refresh prices
    - Triple-tap ...... (within 2 s) show today's weather in Fahrenheit
    - Tap on weather .. back to prices (also returns automatically after 30 s)

  First boot: join the "CYD-Ticker-Setup" hotspot with your phone, pick your WiFi and
  enter your US ZIP code. To change WiFi or ZIP later, hold the screen while powering
  on until it shows "WiFi settings cleared".

  Data: CoinGecko (prices), zippopotam.us (ZIP -> location), Open-Meteo (weather). No keys needed.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ====== USER SETTINGS ======
const char* AP_NAME   = "CYD-Ticker-Setup";   // hotspot name shown during setup
const char* CURRENCY  = "usd";
const char* SYMBOL    = "$";
const uint32_t REFRESH_MS         = 60000;    // automatic price refresh
const uint32_t RETRY_MS           = 10000;    // retry after an error
const uint32_t MIN_TAP_GAP_MS     = 5000;     // min time between manual refreshes (API rate limit)
const uint32_t TAP_GAP_MS         = 1000;     // max pause between taps of one sequence
const uint32_t TRIPLE_WINDOW_MS   = 2000;     // 3 taps within this time = weather page
const uint32_t WEATHER_PAGE_MS    = 30000;    // auto-return from weather page (0 = stay until tap)
const uint32_t WEATHER_CACHE_MS   = 600000;   // reuse weather data for 10 minutes
const int      ROTATION           = 1;        // use 3 if the screen is upside down
const bool     GREEN_RED_CHANGE   = false;    // true = green/red 24h change instead of orange
// ===========================

// ---- Colors (RGB565) ----
const uint16_t BG         = TFT_BLACK;
const uint16_t ORANGE     = 0xFD20;   // main text (255,165,0)
const uint16_t DIM_ORANGE = 0xA300;   // secondary text (165,97,0)

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789    _panel;
  lgfx::Bus_SPI         _bus;
  lgfx::Light_PWM       _light;
  lgfx::Touch_XPT2046   _touch;
public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host   = SPI2_HOST;
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk   = 14;
      c.pin_mosi   = 13;
      c.pin_miso   = 12;
      c.pin_dc     = 2;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs       = 15;
      c.pin_rst      = -1;
      c.panel_width  = 240;
      c.panel_height = 320;
      c.readable     = false;
      c.invert       = false;
      c.rgb_order    = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = 21;
      c.invert      = false;
      c.freq        = 44100;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      // The CYD touch controller sits on its own SPI bus
      auto c = _touch.config();
      c.x_min = 0;   c.x_max = 239;
      c.y_min = 0;   c.y_max = 319;
      c.pin_int    = -1;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.spi_host = SPI3_HOST;
      c.freq     = 1000000;
      c.pin_sclk = 25;
      c.pin_mosi = 32;
      c.pin_miso = 39;
      c.pin_cs   = 33;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX tft;
Preferences prefs;

// ---------------- Prices ----------------
struct Coin {
  const char* id;
  const char* name;
  const char* ticker;
  int         decimals;
  float       price;
  float       change;
  bool        valid;
};

Coin coins[2] = {
  {"bitcoin", "Bitcoin", "BTC", 0, 0, 0, false},
  {"monero",  "Monero",  "XMR", 2, 0, 0, false},
};

uint32_t lastFetch = 0;
uint32_t waitMs = 0;
String lastError = "";

// ---------------- Weather / pages ----------------
enum Page { PAGE_PRICES, PAGE_WEATHER };
Page page = PAGE_PRICES;

struct Weather {
  bool     valid;
  int      temp, high, low, rain, wind, code;
  uint32_t fetchedAt;
};
Weather weather = {false, 0, 0, 0, 0, 0, 0, 0};
String   weatherError = "";
uint32_t weatherShownAt = 0;

String zipCode  = "";
String cityName = "";
float  geoLat = 0, geoLon = 0;

bool validZip(const String& s) {
  if (s.length() != 5) return false;
  for (int i = 0; i < 5; i++) if (!isdigit((unsigned char)s[i])) return false;
  return true;
}

String formatPrice(float v, int decimals) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  String s(buf);
  int dot = s.indexOf('.');
  int intEnd = (dot < 0) ? s.length() : dot;
  for (int i = intEnd - 3; i > 0; i -= 3) s = s.substring(0, i) + "," + s.substring(i);
  return s;
}

void drawCard(int idx) {
  Coin& c = coins[idx];
  const int cardH = 100;
  const int y = 6 + idx * (cardH + 6);

  tft.fillRoundRect(8, y, 304, cardH, 10, BG);
  tft.drawRoundRect(8, y, 304, cardH, 10, ORANGE);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(ORANGE, BG);
  tft.setTextDatum(top_left);
  tft.drawString(c.name, 20, y + 8);
  tft.setTextColor(DIM_ORANGE, BG);
  tft.setTextDatum(top_right);
  tft.drawString(c.ticker, 300, y + 8);

  tft.setTextDatum(middle_left);
  if (!c.valid) {
    tft.setTextColor(DIM_ORANGE, BG);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.drawString("---", 20, y + 55);
    return;
  }
  tft.setTextColor(ORANGE, BG);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  String p = String(SYMBOL) + formatPrice(c.price, c.decimals);
  tft.drawString(p, 20, y + 52);

  bool up = c.change >= 0;
  uint16_t chColor = GREEN_RED_CHANGE ? (up ? TFT_GREEN : TFT_RED) : DIM_ORANGE;
  tft.setTextColor(chColor, BG);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextDatum(bottom_left);
  char ch[24];
  snprintf(ch, sizeof(ch), "%s%.2f%% (24h)", up ? "+" : "", c.change);
  tft.drawString(ch, 20, y + cardH - 6);
}

void drawAll() { drawCard(0); drawCard(1); }

void showStatus(const String& msg, uint16_t color) {
  tft.fillRect(0, 216, 320, 24, BG);
  tft.fillCircle(14, 228, 5, color);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(color, BG);
  tft.setTextDatum(middle_left);
  tft.drawString(msg, 28, 228);
}

String idleStatus() { return WiFi.localIP().toString() + " - tap to refresh"; }

// ---------------- WiFi ----------------
// Used after boot if the connection drops (uses the saved credentials)
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showStatus("Reconnecting to WiFi...", ORANGE);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi lost - retrying";
    return false;
  }
  return true;
}

// Shown while the setup hotspot is running
void apCallback(WiFiManager* wm) {
  tft.fillScreen(BG);
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(ORANGE, BG);
  tft.drawString("WiFi setup", 10, 6);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(ORANGE, BG);
  tft.drawString("1. On your phone, join WiFi:", 10, 42);
  tft.setTextColor(TFT_WHITE, BG);
  tft.drawString(AP_NAME, 28, 64);
  tft.setTextColor(ORANGE, BG);
  tft.drawString("2. A setup page should open.", 10, 96);
  tft.drawString("    If not, visit 192.168.4.1", 10, 118);
  tft.drawString("3. Pick your WiFi, enter your", 10, 152);
  tft.drawString("    US ZIP code, then save.", 10, 174);
}

void setupWiFi() {
  prefs.begin("ticker", false);
  WiFiManager wm;

  // Hold the screen at power-up to forget the saved WiFi and ZIP
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    uint32_t start = millis();
    bool held = true;
    while (millis() - start < 2500) {
      if (!tft.getTouch(&tx, &ty)) { held = false; break; }
      delay(50);
    }
    if (held) {
      wm.resetSettings();
      prefs.remove("zip");
      prefs.remove("geozip");
      tft.setFont(&fonts::FreeSansBold12pt7b);
      tft.setTextColor(ORANGE, BG);
      tft.setTextDatum(middle_center);
      tft.drawString("WiFi settings cleared", 160, 120);
      delay(2000);
    }
  }

  zipCode = prefs.getString("zip", "");

  tft.fillScreen(BG);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(ORANGE, BG);
  tft.setTextDatum(middle_center);
  tft.drawString("Connecting to WiFi...", 160, 120);

  WiFiManagerParameter zipParam("zip", "US ZIP code (5 digits)", zipCode.c_str(), 5,
                                "inputmode=\"numeric\" pattern=\"[0-9]{5}\" maxlength=\"5\"");
  wm.addParameter(&zipParam);
  wm.setAPCallback(apCallback);
  wm.setConnectTimeout(20);

  // No ZIP saved yet (e.g. firmware updated from an older version): open the setup page
  bool ok = validZip(zipCode) ? wm.autoConnect(AP_NAME) : wm.startConfigPortal(AP_NAME);
  if (!ok) ESP.restart();

  String z = zipParam.getValue();
  if (validZip(z)) {
    zipCode = z;
    prefs.putString("zip", zipCode);
  } else {
    ESP.restart();   // no valid ZIP entered: reopen setup
  }
  tft.fillScreen(BG);
}

// ---------------- Prices fetch ----------------
bool fetchPrices() {
  if (!connectWiFi()) return false;

  showStatus("Fetching prices...", ORANGE);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,monero&vs_currencies=")
             + CURRENCY + "&include_24hr_change=true";
  http.setTimeout(15000);
  http.setUserAgent("Mozilla/5.0 (ESP32)");
  if (!http.begin(client, url)) {
    lastError = "HTTP begin failed";
    return false;
  }
  http.addHeader("accept", "application/json");

  int code = http.GET();
  if (code != 200) {
    if (code == 429)      lastError = "API rate limit (429), retrying";
    else if (code < 0)    lastError = "Connection error: " + http.errorToString(code);
    else                  lastError = "HTTP error " + String(code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    lastError = String("JSON error: ") + err.c_str();
    return false;
  }

  String changeKey = String(CURRENCY) + "_24h_change";
  int got = 0;
  for (auto& c : coins) {
    JsonVariant v = doc[c.id][CURRENCY];
    if (v.isNull()) continue;
    c.price  = v.as<float>();
    c.change = doc[c.id][changeKey.c_str()] | 0.0f;
    c.valid  = true;
    got++;
  }
  if (got == 0) {
    lastError = "No price data in reply";
    return false;
  }
  return true;
}

void doRefresh() {
  bool ok = fetchPrices();
  if (ok) {
    drawAll();
    showStatus(idleStatus(), ORANGE);
    waitMs = REFRESH_MS;
  } else {
    showStatus(lastError, TFT_RED);
    waitMs = RETRY_MS;
  }
  lastFetch = millis();
}

// ---------------- Weather ----------------
const char* wmoText(int c) {
  switch (c) {
    case 0:  return "Clear sky";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "Unknown";
  }
}

// ZIP -> latitude/longitude/city (looked up once, then cached in flash)
bool ensureGeo() {
  if (prefs.getString("geozip", "") == zipCode && prefs.getFloat("lat", 999) < 900) {
    geoLat   = prefs.getFloat("lat", 0);
    geoLon   = prefs.getFloat("lon", 0);
    cityName = prefs.getString("city", "");
    return true;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.zippopotam.us/us/" + zipCode;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    weatherError = "HTTP begin failed";
    return false;
  }
  int code = http.GET();
  if (code == 404) {
    weatherError = "ZIP " + zipCode + " not found";
    http.end();
    return false;
  }
  if (code != 200) {
    weatherError = (code < 0) ? ("Connection error: " + http.errorToString(code))
                              : ("ZIP lookup error " + String(code));
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    weatherError = "ZIP data error";
    return false;
  }
  JsonObject pl = doc["places"][0];
  if (pl.isNull()) {
    weatherError = "ZIP " + zipCode + " not found";
    return false;
  }
  const char* latS = pl["latitude"]  | "";
  const char* lonS = pl["longitude"] | "";
  if (!latS[0] || !lonS[0]) {
    weatherError = "No location for ZIP";
    return false;
  }
  geoLat = atof(latS);
  geoLon = atof(lonS);
  String place = String(pl["place name"] | "");
  String state = String(pl["state abbreviation"] | "");
  cityName = place + ", " + state;

  prefs.putString("geozip", zipCode);
  prefs.putFloat("lat", geoLat);
  prefs.putFloat("lon", geoLon);
  prefs.putString("city", cityName);
  return true;
}

bool fetchWeather() {
  if (!connectWiFi()) {
    weatherError = "No WiFi connection";
    return false;
  }
  if (!ensureGeo()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(geoLat, 4)
             + "&longitude=" + String(geoLon, 4)
             + "&current=temperature_2m,weather_code,wind_speed_10m"
             + "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             + "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=1";
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    weatherError = "HTTP begin failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    weatherError = (code < 0) ? ("Connection error: " + http.errorToString(code))
                              : ("Weather error " + String(code));
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    weatherError = "Weather data error";
    return false;
  }
  if (doc["current"].isNull() || doc["daily"].isNull()) {
    weatherError = "No weather data";
    return false;
  }
  weather.temp = (int)roundf(doc["current"]["temperature_2m"].as<float>());
  weather.code = doc["current"]["weather_code"].as<int>();
  weather.wind = (int)roundf(doc["current"]["wind_speed_10m"].as<float>());
  weather.high = (int)roundf(doc["daily"]["temperature_2m_max"][0].as<float>());
  weather.low  = (int)roundf(doc["daily"]["temperature_2m_min"][0].as<float>());
  weather.rain = doc["daily"]["precipitation_probability_max"][0].as<int>();
  weather.valid = true;
  weather.fetchedAt = millis();
  return true;
}

// Draws e.g. "72°F" with the current font, vertically centered on y
void drawTemp(int x, int y, int v, uint16_t color) {
  tft.setTextDatum(middle_left);
  tft.setTextColor(color, BG);
  String n = String(v);
  tft.drawString(n, x, y);
  int w  = tft.textWidth(n);
  int fh = tft.fontHeight();
  int r  = max(2, fh / 14);
  int cx = x + w + r + 3;
  int cy = y - fh / 4;
  tft.drawCircle(cx, cy, r, color);
  tft.drawString("F", cx + r + 3, y);
}

void drawWeather() {
  tft.fillScreen(BG);

  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(ORANGE, BG);
  tft.drawString(cityName, 12, 8);
  tft.setTextDatum(top_right);
  tft.setTextColor(DIM_ORANGE, BG);
  tft.drawString("Today", 308, 8);

  tft.setFont(&fonts::FreeSansBold24pt7b);
  drawTemp(12, 68, weather.temp, ORANGE);

  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextDatum(middle_left);
  tft.setTextColor(ORANGE, BG);
  tft.drawString(wmoText(weather.code), 12, 110);

  tft.drawFastHLine(12, 132, 296, DIM_ORANGE);

  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextDatum(middle_left);
  tft.setTextColor(DIM_ORANGE, BG);
  tft.drawString("High", 12, 156);
  tft.drawString("Low",  170, 156);
  tft.drawString("Rain", 12, 188);
  tft.drawString("Wind", 170, 188);

  drawTemp(72, 156, weather.high, ORANGE);
  drawTemp(230, 156, weather.low, ORANGE);
  tft.setTextDatum(middle_left);
  tft.setTextColor(ORANGE, BG);
  tft.drawString(String(weather.rain) + "%", 72, 188);
  tft.drawString(String(weather.wind) + " mph", 230, 188);

  showStatus("Tap to go back", ORANGE);
}

void showPrices() {
  page = PAGE_PRICES;
  tft.fillScreen(BG);
  drawAll();
  showStatus(idleStatus(), ORANGE);
}

void showWeather() {
  page = PAGE_WEATHER;
  tft.fillScreen(BG);

  bool fresh = weather.valid && (millis() - weather.fetchedAt < WEATHER_CACHE_MS);
  if (!fresh) {
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(ORANGE, BG);
    tft.setTextDatum(middle_center);
    tft.drawString("Loading weather...", 160, 110);
    fresh = fetchWeather();
  }

  if (fresh) {
    drawWeather();
  } else {
    tft.fillScreen(BG);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(TFT_RED, BG);
    tft.setTextDatum(middle_center);
    tft.drawString("Weather unavailable", 160, 90);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.drawString(weatherError, 160, 125);
    showStatus("Tap to go back", ORANGE);
  }
  weatherShownAt = millis();
}

// ---------------- Main ----------------
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(ROTATION);
  tft.setBrightness(200);
  tft.fillScreen(BG);

  setupWiFi();
  drawAll();
}

void loop() {
  static bool first = true;
  static bool wasTouched = false;
  static int tapCount = 0;
  static uint32_t firstTapMs = 0, lastTapMs = 0;

  uint32_t now = millis();
  uint16_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty);
  bool press = touched && !wasTouched && (now - lastTapMs > 120);   // new press, debounced
  wasTouched = touched;

  // ---- Weather page: any tap (or timeout) goes back to prices ----
  if (page == PAGE_WEATHER) {
    if (press || (WEATHER_PAGE_MS > 0 && now - weatherShownAt > WEATHER_PAGE_MS)) {
      showPrices();
      tapCount = 0;
      lastTapMs = millis();
      wasTouched = true;
    }
    delay(50);
    return;
  }

  // ---- Prices page ----
  // A tap sequence that ended with fewer than 3 taps = refresh request
  if (tapCount > 0 && now - lastTapMs > TAP_GAP_MS) {
    tapCount = 0;
    if (millis() - lastFetch < MIN_TAP_GAP_MS) {
      showStatus("Please wait a few seconds...", DIM_ORANGE);
      delay(800);
      showStatus(idleStatus(), ORANGE);
    } else {
      doRefresh();
    }
    wasTouched = true;
    delay(50);
    return;
  }

  if (press) {
    if (tapCount == 0 || now - firstTapMs > TRIPLE_WINDOW_MS) {
      tapCount = 0;
      firstTapMs = now;
    }
    tapCount++;
    lastTapMs = now;
    if (tapCount >= 3) {          // triple tap within the window
      tapCount = 0;
      showWeather();
      wasTouched = true;
      delay(50);
      return;
    }
  }

  // Automatic refresh (not while a tap sequence is in progress)
  if (first || (tapCount == 0 && now - lastFetch >= waitMs)) {
    first = false;
    doRefresh();
    wasTouched = true;
  }
  delay(50);
}
