/*************************************************
 *           USER CONFIGURATION
 *  Edit ONLY this section with your own settings
 *************************************************/

// ---- WiFi ----
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- Finnhub API ----
const char* FINNHUB_API_KEY = "YOUR_FINNHUB_API_KEY";

// ---- Tickers ----
// Change these to your own symbols
const char* TICKERS[] = {
  "SPY",
  "QQQ",
  "BINANCE:BTCUSDT",
  "BINANCE:ETHUSDT"
};

// Labels shown on the display for each ticker above
const char* TICKER_LABELS[] = {
  "S&P",
  "NQ",
  "BTC",
  "ETH"
};
const int NUM_TICKERS = sizeof(TICKERS) / sizeof(TICKERS[0]);

// ---- LED Matrix / MAX7219 Pins ----
// Change these only if you wire it differently
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4        // 4 x 8x8 = 32x8
#define CLK_PIN       18
#define DATA_PIN      23
#define CS_PIN        5

/*************************************************
 *      END OF USER CONFIGURATION
 *  Do not edit below unless you know what
 *  you're doing 🙂
 *************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <time.h>

// Parola object
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// message buffer
#define BUF_SIZE 120
char curMessage[BUF_SIZE] = "Loading...";
bool newMessageAvailable = true;

// timing for price updates
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 30000;  // 30s

// scroll parameters
uint8_t scrollSpeed = 25;
textEffect_t scrollEffect = PA_SCROLL_LEFT;
textPosition_t scrollAlign = PA_LEFT;
uint16_t scrollPause = 0;

// market hours state
bool displayOn = false;

// ===== NTP time setup (Eastern Time) =====
const long gmtOffset_sec = -18000;   // -5 hours
const int daylightOffset_sec = 3600; // +1 hour when DST
const char* ntpServer = "pool.ntp.org";

// ===== Quote struct =====
struct Quote {
  float price;
  float pct;
};

// ===== Finnhub quote (price + % change) =====
Quote getQuote(const char* symbol) {
  HTTPClient http;
  String url = "https://finnhub.io/api/v1/quote?symbol=";
  url += symbol;
  url += "&token=";
  url += FINNHUB_API_KEY;

  http.begin(url);
  int httpCode = http.GET();

  Quote q;
  q.price = NAN;
  q.pct = NAN;

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      float c = doc["c"];   // current price
      float pc = doc["pc"]; // previous close

      if (!isnan(c)) q.price = c;
      if (!isnan(c) && !isnan(pc) && pc != 0.0f) {
        q.pct = ((c - pc) / pc) * 100.0f;
      }
      // Optional debug:
      // Serial.printf("OK %s price=%.2f c=%.2f pc=%.2f pct=%.2f\n",
      //               symbol, q.price, c, pc, q.pct);
    } else {
      Serial.println("JSON error");
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }

  http.end();
  return q;
}

// ===== build scrolling text =====
void updatePrices() {
  char tempMsg[BUF_SIZE] = "";

  for (int i = 0; i < NUM_TICKERS; i++) {
    Quote q = getQuote(TICKERS[i]);
    char tickerStr[28];

    if (!isnan(q.price) && !isnan(q.pct)) {
      snprintf(tickerStr, sizeof(tickerStr),
               "%s: %.0f %.1f%%", TICKER_LABELS[i], q.price, q.pct);
    } else {
      snprintf(tickerStr, sizeof(tickerStr),
               "%s: -- --", TICKER_LABELS[i]);
    }

    if (strlen(tempMsg) + strlen(tickerStr) < BUF_SIZE - 1) {
      strcat(tempMsg, tickerStr);
    } else {
      break;
    }
  }

  strncpy(curMessage, tempMsg, BUF_SIZE - 1);
  curMessage[BUF_SIZE - 1] = '\0';

  newMessageAvailable = true;
  // Serial.println("New msg: " + String(curMessage));
}

// ===== check if US market is open (Eastern time) =====
bool isMarketOpen() {
  static unsigned long lastCheck = 0;
  const unsigned long checkInterval = 60000;  // 1 min

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return false;
  }

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int wday = timeinfo.tm_wday;  // 0=Sun, 1=Mon, ..., 6=Sat

  // Weekend?
  if (wday == 0 || wday == 6) return false;

  int nowMinutes = hour * 60 + minute;
  int marketOpen = 9 * 60 + 30;   // 9:30 AM
  int marketClose = 16 * 60 + 30; // 4:30 PM

  if (millis() - lastCheck > checkInterval) {
    lastCheck = millis();
    Serial.printf("Time: %02d:%02d wday=%d\n", hour, minute, wday);
  }

  return (nowMinutes >= marketOpen && nowMinutes < marketClose);
}

// ===== setup =====
void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Syncing time...");
  delay(2000);

  P.begin();
  P.setIntensity(0);  // start off
  P.displayText(curMessage, scrollAlign, scrollSpeed, scrollPause,
                scrollEffect, scrollEffect);
}

// ===== loop =====
void loop() {
  bool open = isMarketOpen();

  if (open && !displayOn) {
    displayOn = true;
    P.displaySuspend(false);  // resume display engine
    P.setIntensity(4);

    updatePrices();
    lastUpdate = millis();
    P.displayText(curMessage, scrollAlign, scrollSpeed, scrollPause,
                  scrollEffect, scrollEffect);
    P.displayReset();

    Serial.println("Market open — display ON");
  } else if (!open && displayOn) {
    displayOn = false;
    P.displayClear();        // wipe LEDs
    P.displaySuspend(true);  // stop Parola animation engine
    P.setIntensity(0);
    Serial.println("Market closed — display OFF");
  }

  if (displayOn) {
    if (P.displayAnimate()) {
      if (newMessageAvailable) {
        newMessageAvailable = false;
        P.displayReset();
      }
    }

    if (millis() - lastUpdate > updateInterval) {
      updatePrices();
      lastUpdate = millis();
    }
  }

  delay(10);
}
