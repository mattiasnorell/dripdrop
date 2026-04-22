/**
 * DripDrop - Event Logger Implementation
 */

#include "logger.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

EventLogger Logger;

void EventLogger::log(const char* level, const char* event, const char* details) {
  if (_url.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument doc;
  doc["app"]    = EVENT_LOG_APP_ID;
  doc["device"] = _device;
  doc["level"]  = level;
  doc["event"]  = event;

  if (details && strlen(details) > 0) {
    JsonDocument det;
    if (deserializeJson(det, details) == DeserializationError::Ok) {
      doc["details"] = det.as<JsonObject>();
    }
  }

  String payload;
  serializeJson(doc, payload);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(EVENT_LOG_TIMEOUT_MS);
  http.begin(client, _url);
  http.addHeader("Content-Type", "application/json");

  if (_token.length() > 0) {
    http.addHeader("Authorization", String("Bearer ") + _token);
  }

  int code = http.POST(payload);
  DEBUG_PRINTF("[LOG] %s -> HTTP %d\n", event, code);
  http.end();
}
