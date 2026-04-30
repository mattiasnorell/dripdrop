/**
 * DripDrop - Shared API Utilities Implementation
 */

#include "api_utils.h"
#include "config.h"
#include "valves.h"
#include "scenarios.h"
#include <WiFi.h>
#include <time.h>

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
