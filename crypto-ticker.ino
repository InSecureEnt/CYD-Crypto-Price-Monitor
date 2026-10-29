/*
  Bitcoin + Monero price ticker for ESP32-2432S028 (CYD, ST7789 version)
  v2: shows status/errors on screen, retries quickly on failure

  Libraries (Library Manager):
    - LovyanGFX
    - ArduinoJson (v7.x)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ====== USER SETTINGS ======
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* CURRENCY  = "usd";
const char* SYMBOL    = "$";
const uint32_t REFRESH_MS = 60000;   // normal refresh
const uint32_t RETRY_MS   = 10000;   // retry after an error
const int      ROTATION   = 1;       // use 3 if the screen is upside down
// ===========================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
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
      c.invert       = true;
      c.rgb_order    = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = 21;
      c.invert      = false;
      c.freq        = 44100;
      c.pwm_channel = 7;
      _light.config(c);/*
  Bitcoin + Monero price ticker for ESP32-2432S028 (CYD, ST7789 version)
  v2: shows status/errors on screen, retries quickly on failure

  Libraries (Library Manager):
    - LovyanGFX
    - ArduinoJson (v7.x)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ====== USER SETTINGS ======
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* CURRENCY  = "usd";
const char* SYMBOL    = "$";
const uint32_t REFRESH_MS = 60000;   // normal refresh
const uint32_t RETRY_MS   = 10000;   // retry after an error
const int      ROTATION   = 1;       // use 3 if the screen is upside down
// ===========================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
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
      c.invert       = true;
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
    setPanel(&_panel);
  }
};

LGFX tft;

struct Coin {
  const char* id;
  const char* name;
  const char* ticker;
  uint16_t    color;
  int         decimals;
  float       price;
  float       change;
  bool        valid;
};

Coin coins[2] = {
  {"bitcoin", "Bitcoin", "BTC", 0xFD20, 0, 0, 0, false},
  {"monero",  "Monero",  "XMR", 0xFBE0, 2, 0, 0, false},
};

const uint16_t CARD_BG = 0x18E3;
uint32_t lastFetch = 0;
uint32_t waitMs = 0;
String lastError = "";

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

  tft.fillRoundRect(8, y, 304, cardH, 10, CARD_BG);
  tft.drawRoundRect(8, y, 304, cardH, 10, c.color);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(c.color, CARD_BG);
  tft.setTextDatum(top_left);
  tft.drawString(c.name, 20, y + 8);
  tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
  tft.setTextDatum(top_right);
  tft.drawString(c.ticker, 300, y + 8);

  tft.setTextDatum(middle_left);
  if (!c.valid) {
    tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.drawString("---", 20, y + 55);
    return;
  }
  tft.setTextColor(TFT_WHITE, CARD_BG);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  String p = String(SYMBOL) + formatPrice(c.price, c.decimals);
  tft.drawString(p, 20, y + 52);

  bool up = c.change >= 0;
  tft.setTextColor(up ? TFT_GREEN : TFT_RED, CARD_BG);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextDatum(bottom_left);
  char ch[24];
  snprintf(ch, sizeof(ch), "%s%.2f%% (24h)", up ? "+" : "", c.change);
  tft.drawString(ch, 20, y + cardH - 6);
}

void drawAll() { drawCard(0); drawCard(1); }

// Status line at the bottom of the screen
void showStatus(const String& msg, uint16_t color) {
  tft.fillRect(0, 216, 320, 24, TFT_BLACK);
  tft.fillCircle(14, 228, 5, color);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(middle_left);
  tft.drawString(msg, 28, 228);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showStatus("Connecting to WiFi...", TFT_YELLOW);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi failed (check name/password, 2.4GHz)";
    return false;
  }
  return true;
}

bool fetchPrices() {
  if (!connectWiFi()) return false;

  showStatus("Fetching prices...", TFT_YELLOW);

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
  Serial.println(body);

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

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(ROTATION);
  tft.setBrightness(200);
  tft.fillScreen(TFT_BLACK);
  drawAll();
}

void loop() {
  static bool first = true;
  if (first || millis() - lastFetch >= waitMs) {
    first = false;
    bool ok = fetchPrices();
    if (ok) {
      drawAll();
      showStatus("Updated OK  " + WiFi.localIP().toString(), TFT_GREEN);
      waitMs = REFRESH_MS;
    } else {
      showStatus(lastError, TFT_RED);
      waitMs = RETRY_MS;
    }
    lastFetch = millis();
  }
  delay(100);
}
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX tft;

struct Coin {
  const char* id;
  const char* name;
  const char* ticker;
  uint16_t    color;
  int         decimals;
  float       price;
  float       change;
  bool        valid;
};

Coin coins[2] = {
  {"bitcoin", "Bitcoin", "BTC", 0xFD20, 0, 0, 0, false},
  {"monero",  "Monero",  "XMR", 0xFBE0, 2, 0, 0, false},
};

const uint16_t CARD_BG = 0x18E3;
uint32_t lastFetch = 0;
uint32_t waitMs = 0;
String lastError = "";

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

  tft.fillRoundRect(8, y, 304, cardH, 10, CARD_BG);
  tft.drawRoundRect(8, y, 304, cardH, 10, c.color);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(c.color, CARD_BG);
  tft.setTextDatum(top_left);
  tft.drawString(c.name, 20, y + 8);
  tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
  tft.setTextDatum(top_right);
  tft.drawString(c.ticker, 300, y + 8);

  tft.setTextDatum(middle_left);
  if (!c.valid) {
    tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.drawString("---", 20, y + 55);
    return;
  }
  tft.setTextColor(TFT_WHITE, CARD_BG);/*
  Bitcoin + Monero price ticker for ESP32-2432S028 (CYD, ST7789 version)
  v2: shows status/errors on screen, retries quickly on failure

  Libraries (Library Manager):
    - LovyanGFX
    - ArduinoJson (v7.x)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ====== USER SETTINGS ======
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* CURRENCY  = "usd";
const char* SYMBOL    = "$";
const uint32_t REFRESH_MS = 60000;   // normal refresh
const uint32_t RETRY_MS   = 10000;   // retry after an error
const int      ROTATION   = 1;       // use 3 if the screen is upside down
// ===========================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
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
      c.invert       = true;
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
    setPanel(&_panel);
  }
};

LGFX tft;

struct Coin {
  const char* id;
  const char* name;
  const char* ticker;
  uint16_t    color;
  int         decimals;
  float       price;
  float       change;
  bool        valid;
};

Coin coins[2] = {
  {"bitcoin", "Bitcoin", "BTC", 0xFD20, 0, 0, 0, false},
  {"monero",  "Monero",  "XMR", 0xFBE0, 2, 0, 0, false},
};

const uint16_t CARD_BG = 0x18E3;
uint32_t lastFetch = 0;
uint32_t waitMs = 0;
String lastError = "";

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

  tft.fillRoundRect(8, y, 304, cardH, 10, CARD_BG);
  tft.drawRoundRect(8, y, 304, cardH, 10, c.color);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(c.color, CARD_BG);
  tft.setTextDatum(top_left);
  tft.drawString(c.name, 20, y + 8);
  tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
  tft.setTextDatum(top_right);
  tft.drawString(c.ticker, 300, y + 8);

  tft.setTextDatum(middle_left);
  if (!c.valid) {
    tft.setTextColor(TFT_LIGHTGREY, CARD_BG);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.drawString("---", 20, y + 55);
    return;
  }
  tft.setTextColor(TFT_WHITE, CARD_BG);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  String p = String(SYMBOL) + formatPrice(c.price, c.decimals);
  tft.drawString(p, 20, y + 52);

  bool up = c.change >= 0;
  tft.setTextColor(up ? TFT_GREEN : TFT_RED, CARD_BG);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextDatum(bottom_left);
  char ch[24];
  snprintf(ch, sizeof(ch), "%s%.2f%% (24h)", up ? "+" : "", c.change);
  tft.drawString(ch, 20, y + cardH - 6);
}

void drawAll() { drawCard(0); drawCard(1); }

// Status line at the bottom of the screen
void showStatus(const String& msg, uint16_t color) {
  tft.fillRect(0, 216, 320, 24, TFT_BLACK);
  tft.fillCircle(14, 228, 5, color);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(middle_left);
  tft.drawString(msg, 28, 228);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showStatus("Connecting to WiFi...", TFT_YELLOW);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi failed (check name/password, 2.4GHz)";
    return false;
  }
  return true;
}

bool fetchPrices() {
  if (!connectWiFi()) return false;

  showStatus("Fetching prices...", TFT_YELLOW);

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
  Serial.println(body);

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

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(ROTATION);
  tft.setBrightness(200);
  tft.fillScreen(TFT_BLACK);
  drawAll();
}

void loop() {
  static bool first = true;
  if (first || millis() - lastFetch >= waitMs) {
    first = false;
    bool ok = fetchPrices();
    if (ok) {
      drawAll();
      showStatus("Updated OK  " + WiFi.localIP().toString(), TFT_GREEN);
      waitMs = REFRESH_MS;
    } else {
      showStatus(lastError, TFT_RED);
      waitMs = RETRY_MS;
    }
    lastFetch = millis();
  }
  delay(100);
}
  tft.setFont(&fonts::FreeSansBold24pt7b);
  String p = String(SYMBOL) + formatPrice(c.price, c.decimals);
  tft.drawString(p, 20, y + 52);

  bool up = c.change >= 0;
  tft.setTextColor(up ? TFT_GREEN : TFT_RED, CARD_BG);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextDatum(bottom_left);
  char ch[24];
  snprintf(ch, sizeof(ch), "%s%.2f%% (24h)", up ? "+" : "", c.change);
  tft.drawString(ch, 20, y + cardH - 6);
}

void drawAll() { drawCard(0); drawCard(1); }

// Status line at the bottom of the screen
void showStatus(const String& msg, uint16_t color) {
  tft.fillRect(0, 216, 320, 24, TFT_BLACK);
  tft.fillCircle(14, 228, 5, color);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(middle_left);
  tft.drawString(msg, 28, 228);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showStatus("Connecting to WiFi...", TFT_YELLOW);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi failed (check name/password, 2.4GHz)";
    return false;
  }
  return true;
}

bool fetchPrices() {
  if (!connectWiFi()) return false;

  showStatus("Fetching prices...", TFT_YELLOW);

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
  Serial.println(body);

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

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(ROTATION);
  tft.setBrightness(200);
  tft.fillScreen(TFT_BLACK);
  drawAll();
}

void loop() {
  static bool first = true;
  if (first || millis() - lastFetch >= waitMs) {
    first = false;
    bool ok = fetchPrices();
    if (ok) {
      drawAll();
      showStatus("Updated OK  " + WiFi.localIP().toString(), TFT_GREEN);
      waitMs = REFRESH_MS;
    } else {
      showStatus(lastError, TFT_RED);
      waitMs = RETRY_MS;
    }
    lastFetch = millis();
  }
  delay(100);
}
