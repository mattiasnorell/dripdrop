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
 * @version 4.0.0
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <uri/UriBraces.h>
#include <esp_task_wdt.h>
#include <atomic>

#include "config.h"
#include "types.h"
#include "valves.h"
#include "timers.h"
#include "scenarios.h"
#include "modules.h"
#include "mqtt.h"
#include "AppHtml.h"
#include <LittleFS.h>

// =============================================================================
// Global Objects
// =============================================================================

WebServer server(HTTP_PORT);

// =============================================================================
// Global State
// =============================================================================

static unsigned long lastScenarioCheck = 0;
static unsigned long lastWifiCheck = 0;
static unsigned long lastNtpSync = 0;
static std::atomic<bool> ntpSynced{ false };
static bool apMode = false;
static String deviceName = "dripdrop";

// =============================================================================
// Forward Declarations
// =============================================================================

// Setup functions
void setupWiFi();
void setupMdns();
void setupNtp();
void setupWatchdog();
void setupRoutes();
void loadSettings();
void saveSettings();
void handleSystemNameGet();   // GET  /system/name
void handleSystemNamePost();  // POST /system/name

// Loop helpers
void checkWiFiConnection();

// HTTP handlers
void handleRoot();
void handleNotFound();
void handleSystemStatus();
void handleSystemIp();
void handleSystemPing();
void handleSystemTime();
void handleSystemTimePost();
void handleSystemReboot();
void handleValveList();
void handleValveState();      // GET  /valves/{id}/state
void handleValveUpdate();     // POST /valves/{id}
void handleValveOn();         // POST /valves/{id}/on
void handleValveOff();        // POST /valves/{id}/off
void handleValvesAllOff();    // POST /valves/off
void handleTimerGet();        // GET  /timers
void handleTimerPost();       // POST /valves/{id}/timer
void handleTimerAbort();      // DELETE /valves/{id}/timer
void handleScenarioList();    // GET  /scenarios
void handleScenarioAdd();     // POST /scenarios
void handleScenarioUpdate();  // POST /scenarios/{id}
void handleScenarioDelete();  // DELETE /scenarios/{id}
void handleModuleList();      // GET  /modules
void handleModuleScan();      // POST /modules/scan
void handleModuleRegister();  // POST /modules/{uid}/register
void handleModuleUpdate();    // POST /modules/{uid}
void handleModuleRemove();    // DELETE /modules/{uid}
void handleModuleReading();   // GET  /modules/{uid}/reading
void handleSystemMqttGet();   // GET  /system/mqtt
void handleSystemMqttPost();  // POST /system/mqtt

// Utility functions
void sendJsonResponse(int code, const char* message);
void sendJsonError(int code, const char* error);
void sendCorsHeaders();
bool parseJsonBody(JsonDocument& doc);
bool checkApiAuth();
SystemStatus getSystemStatus();

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
  loadSettings();
  Mqtt.begin();

  setupWatchdog();
  setupWiFi();
  setupMdns();
  setupNtp();

  setupRoutes();
  ElegantOTA.begin(&server);
  ElegantOTA.onStart([]() {
    esp_task_wdt_delete(NULL);  // remove this task from watchdog during flash
    DEBUG_PRINTLN(F("[OTA] Update started"));
  });
  ElegantOTA.onEnd([](bool success) {
    if (success) {
      DEBUG_PRINTLN(F("[OTA] Update complete, rebooting"));
    } else {
      esp_task_wdt_add(NULL);   // re-add to watchdog if update failed (no reboot)
      DEBUG_PRINTLN(F("[OTA] Update failed"));
    }
  });

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

  Mqtt.publishEvent(LogLevel::INFO,LogEvent::SYSTEM_BOOT);
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

  if (!ntpSynced) {
    if (currentTime > MIN_VALID_UNIX_TIME) {
      ntpSynced = true;
      lastNtpSync = now;
      DEBUG_PRINTLN(F("NTP synchronized"));

      char d[32];
      snprintf(d, sizeof(d), "{\"currentTime\":%d}", currentTime);
      Mqtt.publishEvent(LogLevel::INFO,LogEvent::SYSTEM_NTP_SYNCED, d);
    }
  } else if (now - lastNtpSync >= NTP_SYNC_INTERVAL_MS) {
    lastNtpSync = now;
  }

  Scenarios.maybeSave(now);
  Mqtt.loop(now);
  ElegantOTA.loop();
  esp_task_wdt_reset();
}

// =============================================================================
// Setup Functions
// =============================================================================

void setupWiFi() {
  if (strlen(WIFI_SSID) == 0) {
    DD_DEBUG_WIFI("No WiFi credentials configured, starting AP mode\n");
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    DD_DEBUG_WIFI("AP started: %s\n", AP_SSID);
    DD_DEBUG_WIFI("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    return;
  }

  DD_DEBUG_WIFI("Connecting to %s", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
    Mqtt.publishEvent(LogLevel::INFO,LogEvent::SYSTEM_WIFI_FAILED);
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

void setupNtp() {
  configTime(TIMEZONE_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
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

  // Scenarios
  server.on("/scenarios", HTTP_GET, handleScenarioList);
  server.on("/scenarios", HTTP_POST, handleScenarioAdd);
  server.on(UriBraces("/scenarios/{}"), HTTP_POST, handleScenarioUpdate);
  server.on(UriBraces("/scenarios/{}"), HTTP_DELETE, handleScenarioDelete);

  // Modules — register /modules/{}/register before /modules/{} so the longer
  // pattern is tested first by the router
  server.on("/modules", HTTP_GET, handleModuleList);
  server.on("/modules/scan", HTTP_POST, handleModuleScan);
  server.on(UriBraces("/modules/{}/register"), HTTP_POST, handleModuleRegister);
  server.on(UriBraces("/modules/{}/reading"), HTTP_GET, handleModuleReading);
  server.on(UriBraces("/modules/{}"), HTTP_POST, handleModuleUpdate);
  server.on(UriBraces("/modules/{}"), HTTP_DELETE, handleModuleRemove);

  // 404 / OPTIONS preflight catch-all
  server.onNotFound(handleNotFound);
}

// =============================================================================
// HTTP Handlers - System
// =============================================================================

void handleRoot() {
  server.send_P(200, "text/html", APP_HTML);
}

void handleNotFound() {
  sendCorsHeaders();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }
  sendJsonError(404, "Not Found");
}

void handleSystemStatus() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  SystemStatus status = getSystemStatus();

  JsonDocument doc;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["uptime"] = status.uptime;
  doc["uptimeFormatted"] = String(status.uptime / 86400000) + "d " + String((status.uptime / 3600000) % 24) + "h " + String((status.uptime / 60000) % 60) + "m";
  doc["freeHeap"] = status.freeHeap;
  doc["wifiConnected"] = status.wifiConnected;
  doc["wifiRssi"] = status.wifiRssi;
  doc["apMode"] = status.apMode;
  doc["ntpSynced"] = status.ntpSynced;
  doc["currentTime"] = status.currentTime;
  doc["activeValves"] = status.activeValves;
  doc["activeScenarios"] = status.activeScenarios;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSystemIp() {
  sendCorsHeaders();
  server.send(200, "text/plain",
              apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
}

void handleSystemPing() {
  sendCorsHeaders();
  sendJsonResponse(200, "pong");
}

void handleSystemTime() {
  sendCorsHeaders();

  JsonDocument doc;
  time_t now = time(nullptr);
  doc["unixTime"] = now;
  doc["synced"] = ntpSynced.load();

  if (ntpSynced) {
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    doc["formatted"] = buffer;
    doc["dayOfWeek"] = timeInfo.tm_wday;
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSystemTimePost() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON body");
    return;
  }

  if (!doc["unixTime"].is<long>()) {
    sendJsonError(400, "unixTime (integer) is required");
    return;
  }

  time_t newTime = doc["unixTime"];
  if (newTime <= MIN_VALID_UNIX_TIME) {
    sendJsonError(400, "unixTime value is invalid");
    return;
  }

  struct timeval tv;
  tv.tv_sec = newTime;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  ntpSynced = true;
  lastNtpSync = millis();

  sendJsonResponse(200, "ok");
}

void handleSystemReboot() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  sendJsonResponse(200, "Rebooting...");
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::SYSTEM_REBOOT);
  delay(500);
  ESP.restart();
}

void handleSystemNameGet() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  doc["name"] = deviceName;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSystemNamePost() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["name"].is<const char*>() || strlen(doc["name"].as<const char*>()) == 0) {
    sendJsonError(400, "name (string) is required");
    return;
  }

  deviceName = doc["name"].as<const char*>();
  saveSettings();
  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - MQTT
// =============================================================================

void handleSystemMqttGet() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  doc["enabled"]   = Mqtt.getEnabled();
  doc["server"]    = Mqtt.getServer();
  doc["port"]      = Mqtt.getPort();
  doc["user"]      = Mqtt.getUser();
  doc["connected"] = Mqtt.isConnected();

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSystemMqttPost() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (doc["enabled"].is<bool>()) {
    Mqtt.setEnabled(doc["enabled"].as<bool>());
  }

  if (doc["server"].is<const char*>()) {
    uint16_t port = doc["port"] | Mqtt.getPort();
    Mqtt.setServer(doc["server"].as<const char*>(), port);
  }

  const char* user = doc["user"] | Mqtt.getUser().c_str();
  const char* password = doc["password"] | "";
  if (doc["user"].is<const char*>() || doc["password"].is<const char*>()) {
    Mqtt.setCredentials(user, password);
  }

  saveSettings();

  Mqtt.disconnect();
  if (Mqtt.getEnabled()) Mqtt.begin();

  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - Valves
// =============================================================================

void handleValveList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  time_t now = time(nullptr);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < Valves.count(); i++) {
    const Valve* valve = Valves.getValve(i);
    if (!valve) continue;

    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = valve->id;
    obj["customName"] = valve->customName[0] ? (const char*)valve->customName : (const char*)nullptr;
    obj["isOn"] = valve->isOn;
    obj["source"] = static_cast<int>(valve->source);
    obj["lastRunStart"] = valve->lastRunStart;
    obj["lastRunEnd"] = valve->lastRunEnd;

    if (Timers.isActive(valve->id, now)) {
      obj["timerRemaining"] = Timers.getRemainingSeconds(valve->id, now);
    }
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleValveState() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  const Valve* valve = Valves.getValve(index);
  time_t now = time(nullptr);

  JsonDocument doc;
  doc["valveId"] = valveId;
  doc["customName"] = valve->customName[0] ? (const char*)valve->customName : (const char*)nullptr;
  doc["isOn"] = valve->isOn;
  doc["source"] = static_cast<int>(valve->source);

  if (Timers.isActive(valveId, now)) {
    doc["timerRemaining"] = Timers.getRemainingSeconds(valveId, now);
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleValveUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (doc["customName"].is<const char*>() || doc["customName"].isNull()) {
    if (doc["customName"].isNull() || doc["customName"].as<const char*>()[0] == '\0') {
      Valves.setCustomName(index, nullptr);
    } else {
      Valves.setCustomName(index, doc["customName"].as<const char*>());
    }
  }

  sendJsonResponse(200, "Valve updated");
}

void handleValveOn() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  Valves.setState(index, true, ValveSource::MANUAL);
  Mqtt.publishValveState(valveId);
  char d[32];
  snprintf(d, sizeof(d), "{\"valveId\":%d}", valveId);
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::VALVE_ON, d);
  sendJsonResponse(200, "ok");
}

void handleValveOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  Timers.abort(valveId);
  Valves.setState(index, false, ValveSource::NONE);
  Mqtt.publishValveState(valveId);
  char d[32];
  snprintf(d, sizeof(d), "{\"valveId\":%d}", valveId);
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::VALVE_OFF, d);
  sendJsonResponse(200, "ok");
}

void handleValvesAllOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  Timers.abortAll();
  Valves.allOff();
  Mqtt.publishAllValveStates();
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::VALVES_ALL_OFF);
  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - Timers
// =============================================================================

void handleTimerGet() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  time_t now = time(nullptr);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    const Timer* timer = Timers.get(i);
    if (!timer) continue;

    JsonObject obj = arr.add<JsonObject>();
    obj["valveId"] = timer->valveId;
    obj["endTime"] = timer->endTime;
    obj["active"] = timer->isActive(now);

    if (timer->isActive(now)) {
      obj["remaining"] = static_cast<long>(timer->endTime - now);
    }
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleTimerPost() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();

  if (!Valves.isValidId(valveId)) {
    sendJsonError(404, "Valve not found");
    return;
  }

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["duration"].is<int>()) {
    sendJsonError(400, "Missing duration");
    return;
  }

  uint32_t duration = doc["duration"];

  if (duration == 0 || duration > MAX_TIMER_DURATION_SEC) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Duration must be 1-%lu seconds", MAX_TIMER_DURATION_SEC);
    sendJsonError(400, msg);
    return;
  }

  if (!Timers.start(valveId, duration)) {
    sendJsonError(500, "Failed to start timer");
    return;
  }

  sendJsonResponse(200, "ok");
}

void handleTimerAbort() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t valveId = server.pathArg(0).toInt();

  if (!Valves.isValidId(valveId)) {
    sendJsonError(404, "Valve not found");
    return;
  }

  Timers.abort(valveId);
  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - Scenarios
// =============================================================================

// GET /scenarios
void handleScenarioList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String output;
  Scenarios.serialize(output);
  server.send(200, "application/json", output);
}

// POST /scenarios
void handleScenarioAdd() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  JsonObject input = doc.as<JsonObject>();
  String newId;
  const char* err = Scenarios.add(input, newId);

  if (err) {
    sendJsonError(400, err);
    return;
  }

  char d[96];
  snprintf(d, sizeof(d), "{\"id\":\"%s\",\"name\":\"%s\"}", newId.c_str(), input["name"].as<const char*>());
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::SCENARIO_ADD, d);

  JsonDocument response;
  response["message"] = "ok";
  response["id"] = newId;

  String output;
  serializeJson(response, output);
  server.send(200, "application/json", output);
}

// POST /scenarios/{id}
void handleScenarioUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String id = server.pathArg(0);

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  JsonObject input = doc.as<JsonObject>();
  const char* err = Scenarios.update(id.c_str(), input);

  if (err) {
    sendJsonError(400, err);
    return;
  }

  char d[96];
  snprintf(d, sizeof(d), "{\"id\":\"%s\",\"name\":\"%s\"}", id.c_str(), input["name"].as<const char*>());
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::SCENARIO_UPDATE, d);
  sendJsonResponse(200, "ok");
}

// DELETE /scenarios/{id}
void handleScenarioDelete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String id = server.pathArg(0);

  if (!Scenarios.remove(id.c_str())) {
    sendJsonError(404, "Scenario not found");
    return;
  }

  char d[48];
  snprintf(d, sizeof(d), "{\"id\":\"%s\"}", id.c_str());
  Mqtt.publishEvent(LogLevel::INFO,LogEvent::SCENARIO_DELETE, d);
  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - Modules
// =============================================================================

// GET /modules
void handleModuleList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String output;
  Modules.serializeRegistered(output);
  server.send(200, "application/json", output);
}

// POST /modules/scan
void handleModuleScan() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  Modules.scanModules();
  String output;
  Modules.serializeScan(output);
  server.send(200, "application/json", output);
}

// POST /modules/{uid}/register
void handleModuleRegister() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String uid = server.pathArg(0);
  if (!Modules.registerModule(uid.c_str())) {
    sendJsonError(409, "Already registered or not found in last scan");
    return;
  }
  sendJsonResponse(200, "Registered");
}

// DELETE /modules/{uid}
void handleModuleRemove() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String uid = server.pathArg(0);
  if (!Modules.removeModule(uid.c_str())) {
    sendJsonError(404, "Not found");
    return;
  }
  sendJsonResponse(200, "Removed");
}

// POST /modules/{uid}
void handleModuleUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String uid = server.pathArg(0);

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (doc["customName"].is<const char*>() || doc["customName"].isNull()) {
    const char* name = doc["customName"].isNull() ? nullptr : doc["customName"].as<const char*>();
    if (!Modules.setCustomName(uid.c_str(), name)) {
      sendJsonError(404, "Module not found");
      return;
    }
  }

  sendJsonResponse(200, "Module updated");
}

// GET /modules/{uid}/reading
void handleModuleReading() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String uid = server.pathArg(0);
  uint8_t addr = Modules.addrForUid(uid.c_str());
  if (addr == 0) {
    sendJsonError(404, "Module not registered");
    return;
  }

  SensorResponse resp;
  if (!Modules.readModule(addr, resp)) {
    sendJsonError(422, "Sensor read failed");
    return;
  }

  JsonDocument doc;
  doc["value"] = resp.value;
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// =============================================================================
// Utility Functions
// =============================================================================

void sendJsonResponse(int code, const char* message) {
  JsonDocument doc;
  doc["message"] = message;

  String output;
  serializeJson(doc, output);
  server.send(code, "application/json", output);
}

void sendJsonError(int code, const char* error) {
  JsonDocument doc;
  doc["error"] = error;

  String output;
  serializeJson(doc, output);
  server.send(code, "application/json", output);
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Max-Age", "600");
}

bool parseJsonBody(JsonDocument& doc) {
  if (!server.hasArg("plain")) {
    DEBUG_API("No request body\n");
    return false;
  }

  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    DEBUG_API("JSON parse error: %s\n", error.c_str());
    return false;
  }

  return true;
}

bool checkApiAuth() {
#if API_AUTH_ENABLED
  if (!server.hasHeader(API_KEY_HEADER)) {
    sendCorsHeaders();
    sendJsonError(401, "Missing API key");
    return false;
  }

  if (server.header(API_KEY_HEADER) != API_KEY) {
    sendCorsHeaders();
    sendJsonError(403, "Invalid API key");
    return false;
  }
#endif

  return true;
}

SystemStatus getSystemStatus() {
  SystemStatus status;
  status.uptime = millis();
  status.freeHeap = ESP.getFreeHeap();
  status.wifiConnected = (WiFi.status() == WL_CONNECTED);
  status.wifiRssi = WiFi.RSSI();
  status.apMode = apMode;
  status.ntpSynced = ntpSynced;
  status.currentTime = time(nullptr);
  status.activeValves = Valves.getActiveCount();
  status.activeScenarios = Scenarios.count();
  return status;
}
