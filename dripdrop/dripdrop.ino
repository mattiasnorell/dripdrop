/**
 * DripDrop - Automated Irrigation System
 * 
 * An ESP8266-based irrigation controller with WiFi connectivity,
 * web API, scheduling, and NTP time synchronization.
 * 
 * Features:
 * - Control up to 4 irrigation valves
 * - Schedule-based automatic irrigation with second precision
 * - One-time timer support
 * - Manual valve control via REST API
 * - NTP time synchronization
 * - OTA firmware updates
 * - mDNS discovery (dripdrop.local)
 * - EEPROM persistence with validation
 * - Watchdog timer for reliability
 * - WiFi auto-reconnection
 * 
 * @author DripDrop Team
 * @version 3.0.0-rc5
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Ticker.h>

#include "config.h"
#include "types.h"
#include "valves.h"
#include "scheduler.h"
#include "timers.h"

// =============================================================================
// Global Objects
// =============================================================================

ESP8266WebServer server(HTTP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
Ticker watchdogTicker;

// =============================================================================
// Global State
// =============================================================================

static unsigned long lastScheduleCheck = 0;
static unsigned long lastWifiCheck = 0;
static unsigned long lastNtpSync = 0;
static bool ntpSynced = false;
static bool apMode = false;
static volatile bool watchdogFlag = false;

// =============================================================================
// Forward Declarations
// =============================================================================

// Setup functions
void setupWiFi();
void setupMdns();
void setupNtp();
void setupWatchdog();
void setupRoutes();

// Loop helpers
void checkWiFiConnection();
void feedWatchdog();

// HTTP handlers
void handleRoot();
void handleOptions();
void handleNotFound();
void handleSystemStatus();
void handleSystemIp();
void handleSystemPing();
void handleSystemTime();
void handleSystemTimePost();
void handleSystemReboot();
void handleValveList();
void handleValveState();
void handleValveOn();
void handleValveOff();
void handleValvesAllOff();
void handleTimerGet();
void handleTimerPost();
void handleTimerAbort();
void handleScheduleList();
void handleScheduleAdd();
void handleScheduleUpdate();
void handleScheduleDelete();
void handleScheduleDeleteAll();

// Utility functions
void sendJsonResponse(int code, const char* message);
void sendJsonError(int code, const char* error);
void sendCorsHeaders();
bool parseJsonBody(JsonDocument& doc);
bool checkApiAuth();
SystemStatus getSystemStatus();

// =============================================================================
// Watchdog Callback
// =============================================================================

void IRAM_ATTR watchdogCallback() {
  watchdogFlag = true;
}

// =============================================================================
// Setup
// =============================================================================

void setup() {
  // Initialize serial first for debug output
  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);
  
  DEBUG_PRINTLN(F("\n\n========================================"));
  DEBUG_PRINTF("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
  DEBUG_PRINTLN(F("========================================\n"));
  
  // Initialize EEPROM early
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize hardware controllers
  Valves.begin();
  Timers.begin();
  Scheduler.begin();
  
  // Setup networking
  setupWiFi();
  setupMdns();
  setupNtp();
  
  // Setup HTTP server
  setupRoutes();
  httpUpdater.setup(&server);
  server.enableCORS(true);
  server.collectHeaders(API_KEY_HEADER);
  server.begin();
  
  // Setup watchdog last (after all initialization)
  setupWatchdog();
  
  DEBUG_PRINTLN(F("\n========================================"));
  DEBUG_PRINTLN(F("Setup complete! Server running."));
  DEBUG_PRINTF("IP Address: %s\n", 
               apMode ? WiFi.softAPIP().toString().c_str() 
                      : WiFi.localIP().toString().c_str());
  DEBUG_PRINTF("Free heap: %u bytes\n", ESP.getFreeHeap());
  DEBUG_PRINTLN(F("========================================\n"));
}

// =============================================================================
// Main Loop
// =============================================================================

void loop() {
  // Feed the watchdog
  feedWatchdog();
  
  // Handle HTTP requests
  server.handleClient();
  
  // Update mDNS
  MDNS.update();
  
  unsigned long now = millis();
  time_t currentTime = time(nullptr);
  
  // Check WiFi connection periodically
  if (now - lastWifiCheck >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiCheck = now;
    checkWiFiConnection();
  }
  
  // Check schedules and timers periodically
  if (now - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL_MS) {
    lastScheduleCheck = now;
    
    if (ntpSynced) {
      Scheduler.check(currentTime);
      Timers.check(currentTime);
    }
  }
  
  // Sync NTP status - check every loop until synced, then hourly
  if (!ntpSynced) {
    if (currentTime > MIN_VALID_UNIX_TIME) {
      ntpSynced = true;
      lastNtpSync = now;
      DEBUG_PRINTLN(F("NTP synchronized"));
    }
  } else if (now - lastNtpSync >= NTP_SYNC_INTERVAL_MS) {
    lastNtpSync = now;
  }
  
  // Small yield to prevent watchdog issues
  yield();
}

// =============================================================================
// Setup Functions
// =============================================================================

void setupWiFi() {
  // Check if credentials are configured
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
  // Use Ticker for software watchdog
  watchdogTicker.attach_ms(WATCHDOG_TIMEOUT_MS / 2, watchdogCallback);
  DEBUG_PRINTLN(F("Watchdog enabled"));
}

void checkWiFiConnection() {
  if (apMode) return;  // Don't check if in AP mode
  
  if (WiFi.status() != WL_CONNECTED) {
    DD_DEBUG_WIFI("Connection lost, reconnecting...\n");
    WiFi.reconnect();
  }
}

void feedWatchdog() {
  if (watchdogFlag) {
    watchdogFlag = false;
    ESP.wdtFeed();  // Feed the hardware watchdog
  }
}

// =============================================================================
// HTTP Route Setup
// =============================================================================

void setupRoutes() {
  // Root & system
  server.on("/", HTTP_GET, handleRoot);
  server.on("/system/status", HTTP_GET, handleSystemStatus);
  server.on("/system/status", HTTP_OPTIONS, handleOptions);
  server.on("/system/ip", HTTP_GET, handleSystemIp);
  server.on("/system/ip", HTTP_OPTIONS, handleOptions);
  server.on("/system/ping", HTTP_GET, handleSystemPing);
  server.on("/system/ping", HTTP_OPTIONS, handleOptions);
  server.on("/system/time", HTTP_GET, handleSystemTime);
  server.on("/system/time", HTTP_POST, handleSystemTimePost);
  server.on("/system/time", HTTP_OPTIONS, handleOptions);
  server.on("/system/reboot", HTTP_POST, handleSystemReboot);
  server.on("/system/reboot", HTTP_OPTIONS, handleOptions);
  
  // Valves
  server.on("/valves", HTTP_GET, handleValveList);
  server.on("/valves", HTTP_OPTIONS, handleOptions);
  server.on("/valves/off", HTTP_POST, handleValvesAllOff);
  server.on("/valves/off", HTTP_OPTIONS, handleOptions);
  server.on("/valve/state", HTTP_GET, handleValveState);
  server.on("/valve/state", HTTP_OPTIONS, handleOptions);
  server.on("/valve/state/on", HTTP_POST, handleValveOn);
  server.on("/valve/state/on", HTTP_OPTIONS, handleOptions);
  server.on("/valve/state/off", HTTP_POST, handleValveOff);
  server.on("/valve/state/off", HTTP_OPTIONS, handleOptions);
  
  // Timers
  server.on("/timer", HTTP_GET, handleTimerGet);
  server.on("/timer", HTTP_POST, handleTimerPost);
  server.on("/timer", HTTP_OPTIONS, handleOptions);
  server.on("/timer/abort", HTTP_POST, handleTimerAbort);
  server.on("/timer/abort", HTTP_OPTIONS, handleOptions);
  
  // Schedules
  server.on("/schedule/list", HTTP_GET, handleScheduleList);
  server.on("/schedule/list", HTTP_OPTIONS, handleOptions);
  server.on("/schedule/add", HTTP_POST, handleScheduleAdd);
  server.on("/schedule/add", HTTP_OPTIONS, handleOptions);
  server.on("/schedule/update", HTTP_POST, handleScheduleUpdate);
  server.on("/schedule/update", HTTP_OPTIONS, handleOptions);
  server.on("/schedule/delete", HTTP_POST, handleScheduleDelete);
  server.on("/schedule/delete", HTTP_OPTIONS, handleOptions);
  server.on("/schedule/deleteAll", HTTP_POST, handleScheduleDeleteAll);
  server.on("/schedule/deleteAll", HTTP_OPTIONS, handleOptions);
  
  // 404
  server.onNotFound(handleNotFound);
}

// =============================================================================
// HTTP Handlers - System
// =============================================================================

void handleRoot() {
  static const char html[] PROGMEM = R"(<!DOCTYPE html>
<html><head><title>DripDrop</title><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:system-ui;max-width:600px;margin:40px auto;padding:20px;background:#f5f5f5}
h1{color:#2d5a27}a{color:#4a7c43}</style></head>
<body><h1>DripDrop Irrigation</h1><p>API is running. <a href="/system/status">System Status</a></p></body></html>)";
  
  server.send_P(200, "text/html", html);
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

void handleNotFound() {
  sendJsonError(404, "Not Found");
}

void handleSystemStatus() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  
  SystemStatus status = getSystemStatus();
  
  JsonDocument doc;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["uptime"] = status.uptime;
  doc["uptimeFormatted"] = String(status.uptime / 86400000) + "d " + 
                           String((status.uptime / 3600000) % 24) + "h " +
                           String((status.uptime / 60000) % 60) + "m";
  doc["freeHeap"] = status.freeHeap;
  doc["wifiConnected"] = status.wifiConnected;
  doc["wifiRssi"] = status.wifiRssi;
  doc["apMode"] = status.apMode;
  doc["ntpSynced"] = status.ntpSynced;
  doc["currentTime"] = status.currentTime;
  doc["activeValves"] = status.activeValves;
  doc["activeSchedules"] = status.activeSchedules;
  
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
  doc["synced"] = ntpSynced;
  
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
  delay(500);
  ESP.restart();
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
    obj["isOn"] = valve->isOn;
    obj["source"] = static_cast<int>(valve->source);
    obj["lastRunStart"] = valve->lastRunStart;
    obj["lastRunEnd"] = valve->lastRunEnd;
    
    // Add timer info if active
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
  
  if (!server.hasArg("valveId")) {
    sendJsonError(400, "Missing valveId parameter");
    return;
  }

  uint8_t valveId = server.arg("valveId").toInt();
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  const Valve* valve = Valves.getValve(index);
  time_t now = time(nullptr);

  JsonDocument doc;
  doc["valveId"] = valveId;
  doc["isOn"] = valve->isOn;
  doc["source"] = static_cast<int>(valve->source);

  if (Timers.isActive(valveId, now)) {
    doc["timerRemaining"] = Timers.getRemainingSeconds(valveId, now);
  }
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleValveOn() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["valveId"].is<int>()) {
    sendJsonError(400, "Missing valveId");
    return;
  }

  uint8_t valveId = doc["valveId"];
  int8_t index = Valves.findByValveId(valveId);
  
  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }
  
  Valves.setState(index, true, ValveSource::MANUAL);
  sendJsonResponse(200, "ok");
}

void handleValveOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["valveId"].is<int>()) {
    sendJsonError(400, "Missing valveId");
    return;
  }

  uint8_t valveId = doc["valveId"];
  int8_t index = Valves.findByValveId(valveId);

  if (index < 0) {
    sendJsonError(404, "Valve not found");
    return;
  }

  // Cancel any timer for this valve
  Timers.abort(valveId);

  Valves.setState(index, false, ValveSource::NONE);
  sendJsonResponse(200, "ok");
}

void handleValvesAllOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  
  Timers.abortAll();
  Valves.allOff();
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

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["valveId"].is<int>() || !doc["duration"].is<int>()) {
    sendJsonError(400, "Missing valveId or duration");
    return;
  }

  uint8_t valveId = doc["valveId"];
  uint32_t duration = doc["duration"];

  if (!Valves.isValidId(valveId)) {
    sendJsonError(404, "Valve not found");
    return;
  }

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

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["valveId"].is<int>()) {
    sendJsonError(400, "Missing valveId");
    return;
  }

  uint8_t valveId = doc["valveId"];

  if (!Valves.isValidId(valveId)) {
    sendJsonError(404, "Valve not found");
    return;
  }

  Timers.abort(valveId);
  sendJsonResponse(200, "ok");
}

// =============================================================================
// HTTP Handlers - Schedules
// =============================================================================

void handleScheduleList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  
  for (uint8_t i = 0; i < Scheduler.maxSchedules(); i++) {
    const Schedule* schedule = Scheduler.get(i);
    if (!schedule || schedule->isEmpty()) continue;

    JsonObject obj = arr.add<JsonObject>();
    obj["scheduleId"] = i;
    obj["valveId"] = schedule->valveId;
    obj["fromHour"] = schedule->fromHour;
    obj["fromMinute"] = schedule->fromMinute;
    obj["duration"] = schedule->duration;
    
    // Days as array of booleans
    JsonArray days = obj["days"].to<JsonArray>();
    for (int8_t bit = 7; bit >= 1; bit--) {
      days.add((schedule->days & (1 << bit)) != 0);
    }
  }
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleScheduleAdd() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["valveId"].is<int>()) {
    sendJsonError(400, "Missing valveId");
    return;
  }

  uint8_t valveId = doc["valveId"];
  uint8_t fromHour = doc["fromHour"] | 0;
  uint8_t fromMinute = doc["fromMinute"] | 0;
  uint16_t duration = doc["duration"] | 300;  // Default 5 minutes
  
  // Validate
  if (!Valves.isValidId(valveId)) {
    sendJsonError(400, "Invalid valveId");
    return;
  }

  if (!SchedulerClass::isValid(fromHour, fromMinute, duration)) {
    sendJsonError(400, "Invalid schedule parameters");
    return;
  }

  // Parse days array
  uint8_t days = 0;
  if (doc["days"].is<JsonArray>()) {
    JsonArray daysArr = doc["days"];
    for (uint8_t i = 0; i < 7 && i < daysArr.size(); i++) {
      if (daysArr[i].as<bool>()) {
        days |= (1 << (7 - i));
      }
    }
  }

  int8_t slot = Scheduler.add(valveId, fromHour, fromMinute, duration, days);
  
  if (slot < 0) {
    sendJsonError(400, "No empty schedule slots available");
    return;
  }
  
  JsonDocument response;
  response["message"] = "ok";
  response["scheduleId"] = slot;
  
  String output;
  serializeJson(response, output);
  server.send(200, "application/json", output);
}

void handleScheduleUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["scheduleId"].is<int>()) {
    sendJsonError(400, "Missing scheduleId");
    return;
  }

  uint8_t scheduleId = doc["scheduleId"];

  if (scheduleId >= Scheduler.maxSchedules()) {
    sendJsonError(400, "Invalid scheduleId");
    return;
  }

  // Get existing schedule
  Schedule* existing = Scheduler.get(scheduleId);
  if (!existing || existing->isEmpty()) {
    sendJsonError(404, "Schedule not found");
    return;
  }

  // Use existing values as defaults
  uint8_t valveId = doc["valveId"] | existing->valveId;
  uint8_t fromHour = doc["fromHour"] | existing->fromHour;
  uint8_t fromMinute = doc["fromMinute"] | existing->fromMinute;
  uint16_t duration = doc["duration"] | existing->duration;
  
  // Validate
  if (!Valves.isValidId(valveId)) {
    sendJsonError(400, "Invalid valveId");
    return;
  }

  if (!SchedulerClass::isValid(fromHour, fromMinute, duration)) {
    sendJsonError(400, "Invalid schedule parameters");
    return;
  }

  // Parse days array or keep existing
  uint8_t days = existing->days;
  if (doc["days"].is<JsonArray>()) {
    days = 0;
    JsonArray daysArr = doc["days"];
    for (uint8_t i = 0; i < 7 && i < daysArr.size(); i++) {
      if (daysArr[i].as<bool>()) {
        days |= (1 << (7 - i));
      }
    }
  }

  if (!Scheduler.update(scheduleId, valveId, fromHour, fromMinute, duration, days)) {
    sendJsonError(500, "Failed to update schedule");
    return;
  }
  
  sendJsonResponse(200, "ok");
}

void handleScheduleDelete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["scheduleId"].is<int>()) {
    sendJsonError(400, "Missing scheduleId");
    return;
  }

  uint8_t scheduleId = doc["scheduleId"];

  if (!Scheduler.remove(scheduleId)) {
    sendJsonError(400, "Invalid scheduleId");
    return;
  }
  
  sendJsonResponse(200, "ok");
}

void handleScheduleDeleteAll() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  
  Scheduler.removeAll();
  sendJsonResponse(200, "ok");
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
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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
  status.activeSchedules = Scheduler.getActiveCount();
  return status;
}
