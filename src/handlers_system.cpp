/**
 * DripDrop - System & MQTT HTTP Handlers
 *
 * Handles: /, /system/status, /system/ip, /system/ping, /system/time,
 *          /system/reboot, /system/name, /system/mqtt.
 */

#include "api_utils.h"
#include "config.h"
#include "mqtt.h"
#include <WiFi.h>
#include <time.h>

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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SYSTEM_REBOOT);
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

void handleSystemWifiGet() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (apMode) {
    doc["ssid"] = nullptr;
    doc["connected"] = false;
    doc["apMode"] = true;
  } else {
    doc["ssid"] = WiFi.SSID();
    doc["connected"] = WiFi.status() == WL_CONNECTED;
    doc["apMode"] = false;
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleSystemWifiPost() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["ssid"].is<const char*>() || strlen(doc["ssid"].as<const char*>()) == 0) {
    sendJsonError(400, "ssid (string) is required");
    return;
  }

  wifiSsid = doc["ssid"].as<const char*>();
  if (doc["password"].is<const char*>()) {
    wifiPassword = doc["password"].as<const char*>();
  }

  saveSettings();

  apMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  sendJsonResponse(200, "ok");
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
