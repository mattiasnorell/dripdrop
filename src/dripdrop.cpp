/**
 * DripDrop - Automated Irrigation System
 *
 * ESP32-based irrigation controller with WiFi connectivity,
 * web API, scenario-based automation, and NTP time synchronization.
 *
 * Features:
 * - Control up to 4 irrigation valves
 * - Scenario-based if-this-then-that automation
 * - One-time timer support
 * - Manual valve control via REST API
 * - I2C sensor board integration
 * - NTP time synchronization
 * - OTA firmware updates
 * - mDNS discovery (dripdrop.local)
 * - LittleFS persistence
 * - WiFi auto-reconnection
 *
 * Requires Arduino ESP32 core v2.x+ (for UriBraces path parameter support).
 *
 * @version 4.0.8
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <uri/UriBraces.h>
#include <esp_task_wdt.h>
#include <atomic>
#include <LittleFS.h>

#include "config.h"
#include "types.h"
#include "valves.h"
#include "timers.h"
#include "scenarios.h"
#include "modules.h"
#include "mqtt.h"
#include "AppHtml.h"
#include "api_utils.h"
#include "display.h"

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
void setupRoutes();
void loadSettings();
void checkWiFiConnection();

// Forward Declarations — HTTP Handlers
void handleRoot();
void handleStaticFile();
void handleFsInfo();
void handleFsList();
void handleFsDirDelete();
void handleFsUploadComplete();
void handleFsUpload();
void handleOtaUploadComplete();
void handleOtaUpload();
void handleOtaFsUploadComplete();
void handleOtaFsUpload();
void handleSystemStatus();
void handleSystemIp();
void handleSystemPing();
void handleSystemTime();
void handleSystemTimePost();
void handleSystemReboot();
void handleSystemNameGet();
void handleSystemNamePost();
void handleSystemMqttGet();
void handleSystemMqttPost();
void handleSystemWifiGet();
void handleSystemWifiPost();
void handleValveList();
void handleValveState();
void handleValveUpdate();
void handleValveOn();
void handleValveOff();
void handleValvesAllOff();
void handleTimerGet();
void handleTimerPost();
void handleTimerAbort();
void handleScenarioList();
void handleScenarioAdd();
void handleScenarioUpdate();
void handleScenarioDelete();
void handleScenarioRun();
void handleModuleList();
void handleModuleScan();
void handleModuleRegister();
void handleModuleUpdate();
void handleModuleRemove();
void handleModuleReading();
void handleModuleCommand();
void handleConfigExport();
void handleConfigImport();

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

  Valves.begin();
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

    if (ntpSynced) {
      Scenarios.check(currentTime);
      Timers.check(currentTime);
    }
  }

  if (now - lastNtpSync >= NTP_SYNC_INTERVAL_MS) {
    lastNtpSync = now;
  }

  Scenarios.maybeSave(now);
  Scenarios.drainCallUrlQueue();
  Mqtt.loop(now);
  Display.update(currentTime, apMode,
      apMode ? String(MDNS_HOSTNAME) + ".local" : WiFi.localIP().toString());
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
  // Parse __DATE__ ("Jun  6 2026") and __TIME__ ("14:30:00") into a Unix timestamp
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int day, year, hour, min, sec;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
  struct tm t = {};
  t.tm_year  = year - 1900;
  t.tm_mon   = (strstr(months, mon) - months) / 3;
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

// =============================================================================
// HTTP Route Setup
// =============================================================================

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);

  // System
  server.on("/system/status", HTTP_GET, handleSystemStatus);
  server.on("/system/ip", HTTP_GET, handleSystemIp);
  server.on("/system/ping", HTTP_GET, handleSystemPing);
  server.on("/system/time", HTTP_GET, handleSystemTime);
  server.on("/system/time", HTTP_POST, handleSystemTimePost);
  server.on("/system/reboot", HTTP_POST, handleSystemReboot);
  server.on("/system/name", HTTP_GET, handleSystemNameGet);
  server.on("/system/name", HTTP_POST, handleSystemNamePost);
  server.on("/system/mqtt", HTTP_GET, handleSystemMqttGet);
  server.on("/system/mqtt", HTTP_POST, handleSystemMqttPost);
  server.on("/system/wifi", HTTP_GET, handleSystemWifiGet);
  server.on("/system/wifi", HTTP_POST, handleSystemWifiPost);

  // Valves
  server.on("/valves", HTTP_GET, handleValveList);
  server.on("/valves/off", HTTP_POST, handleValvesAllOff);
  server.on(UriBraces("/valves/{}/state"), HTTP_GET, handleValveState);
  server.on(UriBraces("/valves/{}"), HTTP_POST, handleValveUpdate);
  server.on(UriBraces("/valves/{}/on"), HTTP_POST, handleValveOn);
  server.on(UriBraces("/valves/{}/off"), HTTP_POST, handleValveOff);

  // Timers
  server.on("/timers", HTTP_GET, handleTimerGet);
  server.on(UriBraces("/valves/{}/timer"), HTTP_POST, handleTimerPost);
  server.on(UriBraces("/valves/{}/timer"), HTTP_DELETE, handleTimerAbort);

  // Scenarios — register /scenarios/{}/run before /scenarios/{} (longer pattern first)
  server.on("/scenarios", HTTP_GET, handleScenarioList);
  server.on("/scenarios", HTTP_POST, handleScenarioAdd);
  server.on(UriBraces("/scenarios/{}/run"), HTTP_POST, handleScenarioRun);
  server.on(UriBraces("/scenarios/{}"), HTTP_POST, handleScenarioUpdate);
  server.on(UriBraces("/scenarios/{}"), HTTP_DELETE, handleScenarioDelete);

  // Modules — register /modules/{}/register before /modules/{} so the longer
  // pattern is tested first by the router
  server.on("/modules", HTTP_GET, handleModuleList);
  server.on("/modules/scan", HTTP_POST, handleModuleScan);
  server.on(UriBraces("/modules/{}/register"), HTTP_POST, handleModuleRegister);
  server.on(UriBraces("/modules/{}/reading"), HTTP_GET, handleModuleReading);
  server.on(UriBraces("/modules/{}/command"), HTTP_POST, handleModuleCommand);
  server.on(UriBraces("/modules/{}"), HTTP_POST, handleModuleUpdate);
  server.on(UriBraces("/modules/{}"), HTTP_DELETE, handleModuleRemove);

  // Config backup/restore
  server.on("/config/export", HTTP_GET,  handleConfigExport);
  server.on("/config/import", HTTP_POST, handleConfigImport);

  // Filesystem inspection
  server.on("/fs/info", HTTP_GET, handleFsInfo);
  server.on("/fs/list", HTTP_GET, handleFsList);
  // Filesystem: clear a directory (used by make ota-webapp before re-upload)
  server.on("/fs/dir", HTTP_DELETE, handleFsDirDelete);
  // Filesystem: upload a single file (used by make ota-webapp)
  server.on("/fs/upload", HTTP_POST, handleFsUploadComplete, handleFsUpload);

  // OTA updates (used by make ota-firmware / ota-fs)
  server.on("/ota/upload",    HTTP_POST, handleOtaUploadComplete,   handleOtaUpload);
  server.on("/ota/upload-fs", HTTP_POST, handleOtaFsUploadComplete, handleOtaFsUpload);

  // Static file serving + SPA fallback (registered last)
  server.onNotFound(handleStaticFile);
}
