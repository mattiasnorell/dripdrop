/**
 * DripDrop - Valve HTTP Handlers
 *
 * Handles: /valves, /valves/{id}/state, /valves/{id},
 *          /valves/{id}/on, /valves/{id}/off, /valves/off
 */

#include "api_utils.h"
#include "valves.h"
#include "timers.h"
#include "mqtt.h"
#include <time.h>

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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::VALVE_ON, d);
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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::VALVE_OFF, d);
  sendJsonResponse(200, "ok");
}

void handleValvesAllOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  Timers.abortAll();
  Valves.allOff();
  Mqtt.publishAllValveStates();
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::VALVES_ALL_OFF);
  sendJsonResponse(200, "ok");
}
