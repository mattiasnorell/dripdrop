/**
 * DripDrop - Automated Irrigation System
 *
 * ESP32-based irrigation controller with WiFi connectivity,
 * web API, scenario-based automation, and NTP time synchronization.
 *
 * Features:
 * - Control up to 4 irrigation relays
 * - Scenario-based if-this-then-that automation
 * - One-time timer support
 * - Manual relay control via REST API
 * - I2C sensor board integration
 * - NTP time synchronization
 * - OTA firmware updates
 * - mDNS discovery (dripdrop.local)
 * - LittleFS persistence
 * - WiFi auto-reconnection
 *
 * Requires Arduino ESP32 core v2.x+ (for UriBraces path parameter support).
 *
 * @version 4.5.1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <atomic>
#include <LittleFS.h>

#include "config.h"
#include "types.h"
#include "relays.h"
#include "timers.h"
#include "scenarios.h"
#include "modules.h"
#include "mqtt.h"
#include "AppHtml.h"
#include "api_utils.h"
#include "display.h"
#include "routes.h"

// =============================================================================
// Global Objects
// =============================================================================

WebServer server(HTTP_PORT);

// =============================================================================
// Global State
// =============================================================================

static unsigned long lastScenarioCheck = 0;
static unsigned long lastWifiCheck = 0;
unsigned long lastNtpSync = 0;
std::atomic<bool> ntpSynced{ false };
bool apMode = false;
String deviceName = "dripdrop";
String wifiSsid = "";
String wifiPassword = "";

// =============================================================================
// Forward Declarations — Setup & Loop Helpers
// =============================================================================

void setupWiFi();
void setupMdns();
void setupNtp();
void setupWatchdog();
void loadSettings();
void checkWiFiConnection();

// HTTP route registration + handler declarations live in routes.cpp / routes.h.

// =============================================================================
// Setup
// =============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);

  DEBUG_PRINTLN(F("\n\n========================================"));
  DEBUG_PRINTF("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
  DEBUG_PRINTLN(F("========================================\n"));

  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN(F("LittleFS mount failed!"));
  } else {
    DEBUG_PRINTLN(F("LittleFS mounted"));
  }

  Relays.begin();
  Timers.begin();
  Scenarios.begin();
  Modules.begin();
  Display.begin();
  loadSettings();
  Mqtt.begin();

  setupWatchdog();
  setupWiFi();
  setupMdns();
  setupNtp();

  setupRoutes();

  const char* headersToCollect[] = { API_KEY_HEADER };
  server.collectHeaders(headersToCollect, 1);
  server.begin();

  DEBUG_PRINTLN(F("\n========================================"));
  DEBUG_PRINTLN(F("Setup complete! Server running."));
  DEBUG_PRINTF("IP Address: %s\n",
               apMode ? WiFi.softAPIP().toString().c_str()
                      : WiFi.localIP().toString().c_str());
  DEBUG_PRINTF("Free heap: %lu bytes\n", ESP.getFreeHeap());
  DEBUG_PRINTLN(F("========================================\n"));

  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SYSTEM_BOOT);
}

// =============================================================================
// Main Loop
// =============================================================================

void loop() {
  server.handleClient();

  unsigned long now = millis();
  time_t currentTime = time(nullptr);

  if (now - lastWifiCheck >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiCheck = now;
    checkWiFiConnection();
  }

  if (now - lastScenarioCheck >= SCENARIO_CHECK_INTERVAL_MS) {
    lastScenarioCheck = now;

    Scenarios.check(currentTime);
    Timers.check(currentTime);
  }

  // NTP resync is handled automatically in the background by the SNTP client
  // configured in setupNtp(); no periodic action is required here.

  Scenarios.maybeSave(now);
  Scenarios.drainCallUrlQueue();
  Mqtt.loop(now);

  static unsigned long lastDisplayMs = 0;
  if (now - lastDisplayMs >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayMs = now;

    // Rebuild the IP/host string only when the connection state changes —
    // toString() allocates a String, so we avoid doing it every second.
    static String displayIp;
    static bool ipApMode = true;
    static wl_status_t ipWifiStatus = (wl_status_t)0xFF;
    wl_status_t st = WiFi.status();
    if (displayIp.length() == 0 || apMode != ipApMode || st != ipWifiStatus) {
      ipApMode = apMode;
      ipWifiStatus = st;
      displayIp = apMode ? String(MDNS_HOSTNAME) + ".local"
                         : WiFi.localIP().toString();
    }
    Display.update(currentTime, apMode, displayIp);
  }

  esp_task_wdt_reset();
}

// =============================================================================
// Setup Functions
// =============================================================================

void setupWiFi() {
  if (wifiSsid.length() == 0) {
    DD_DEBUG_WIFI("No WiFi credentials configured, starting AP mode\n");
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    DD_DEBUG_WIFI("AP started: %s\n", AP_SSID);
    DD_DEBUG_WIFI("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    return;
  }

  DD_DEBUG_WIFI("Connecting to %s", wifiSsid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_TIMEOUT_SEC) {
    esp_task_wdt_reset();
    delay(1000);
    DEBUG_PRINT(".");
    attempts++;
  }
  DEBUG_PRINTLN();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    DD_DEBUG_WIFI("Connected!\n");
    DD_DEBUG_WIFI("IP: %s\n", WiFi.localIP().toString().c_str());
    DD_DEBUG_WIFI("RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    DD_DEBUG_WIFI("Connection failed, starting AP mode\n");
    Mqtt.publishEvent(LogLevel::INFO, LogEvent::SYSTEM_WIFI_FAILED);
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    DD_DEBUG_WIFI("AP started: %s\n", AP_SSID);
  }
}


void setupMdns() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    DEBUG_PRINTF("mDNS started: %s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("dripdrop", "tcp", HTTP_PORT);
  } else {
    DEBUG_PRINTLN(F("mDNS failed to start"));
  }
}

static time_t buildTimestamp() {
  // Parse __DATE__ ("Jun  6 2026") and __TIME__ ("14:30:00") into a Unix timestamp.
  // Hand-parsed with atoi instead of sscanf — the scanf family pulls in ~17 KB of
  // newlib code that would otherwise only be used by this one-shot boot call.
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = { __DATE__[0], __DATE__[1], __DATE__[2], '\0' };
  int day  = atoi(__DATE__ + 4);  // atoi skips the leading space for single-digit days
  int year = atoi(__DATE__ + 7);
  int hour = atoi(__TIME__);      // atoi stops at the ':' separators
  int min  = atoi(__TIME__ + 3);
  int sec  = atoi(__TIME__ + 6);
  struct tm t = {};
  t.tm_year  = year - 1900;
  const char* mp = strstr(months, mon);
  t.tm_mon   = mp ? (int)(mp - months) / 3 : 0;
  t.tm_mday  = day;
  t.tm_hour  = hour;
  t.tm_min   = min;
  t.tm_sec   = sec;
  t.tm_isdst = -1;
  return mktime(&t);
}

void setupNtp() {
  configTime(TIMEZONE_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);

  // Set clock to compile-time as fallback so scenarios run even without NTP.
  // The SNTP client will silently overwrite this with real time once it syncs.
  time_t fallback = buildTimestamp();
  struct timeval tv = { fallback, 0 };
  settimeofday(&tv, nullptr);
  ntpSynced = true;
  lastNtpSync = millis();

  DEBUG_PRINTF("Fallback time set to build timestamp: %ld\n", (long)fallback);
  DEBUG_PRINTLN(F("NTP configured"));
}

void setupWatchdog() {
  esp_err_t err = esp_task_wdt_init(WATCHDOG_TIMEOUT_MS / 1000, true);
  if (err != ESP_OK) {
    DEBUG_PRINTF("Watchdog config failed: %d\n", err);
    return;
  }

  err = esp_task_wdt_add(NULL);
  if (err != ESP_OK) {
    DEBUG_PRINTF("Watchdog task add failed: %d\n", err);
  } else {
    DEBUG_PRINTF("Watchdog enabled (%lus timeout)\n", WATCHDOG_TIMEOUT_MS / 1000);
  }
}

void checkWiFiConnection() {
  if (apMode) return;

  if (WiFi.status() != WL_CONNECTED) {
    DD_DEBUG_WIFI("Connection lost, reconnecting...\n");
    WiFi.reconnect();
  }
}

void loadSettings() {
  File file = LittleFS.open(SETTINGS_FILE, "r");
  if (!file) return;

  JsonDocument doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok) {
    if (doc["name"].is<const char*>())      deviceName = doc["name"].as<const char*>();

    if (doc["mqttEnabled"].is<bool>())        Mqtt.setEnabled(doc["mqttEnabled"].as<bool>());
    if (doc["mqttServer"].is<const char*>()) Mqtt.setServer(doc["mqttServer"].as<const char*>(), doc["mqttPort"] | MQTT_PORT);
    if (doc["mqttUser"].is<const char*>())   Mqtt.setCredentials(doc["mqttUser"].as<const char*>(), doc["mqttPassword"] | "");

    if (doc["wifiSsid"].is<const char*>())     wifiSsid = doc["wifiSsid"].as<const char*>();
    if (doc["wifiPassword"].is<const char*>()) wifiPassword = doc["wifiPassword"].as<const char*>();
  }
  file.close();
  DEBUG_PRINTF("Settings loaded (name=%s)\n", deviceName.c_str());
}

void saveSettings() {
  File file = LittleFS.open(SETTINGS_FILE, "w");
  if (!file) return;

  JsonDocument doc;
  doc["name"]     = deviceName;

  doc["mqttEnabled"]  = Mqtt.getEnabled();
  if (Mqtt.getServer().length() > 0) {
    doc["mqttServer"]   = Mqtt.getServer();
    doc["mqttPort"]     = Mqtt.getPort();
    doc["mqttUser"]     = Mqtt.getUser();
  }

  if (wifiSsid.length() > 0)     doc["wifiSsid"]     = wifiSsid;
  if (wifiPassword.length() > 0) doc["wifiPassword"] = wifiPassword;

  serializeJson(doc, file);
  file.close();
}
