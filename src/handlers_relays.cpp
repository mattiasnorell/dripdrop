/**
 * DripDrop - Relay HTTP Handlers
 *
 * Handles: /relays, /relays/{id}/state, /relays/{id},
 *          /relays/{id}/on, /relays/{id}/off, /relays/off
 */

#include "api_utils.h"
#include "relays.h"
#include "timers.h"
#include "mqtt.h"
#include <time.h>

void handleRelayList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  time_t now = time(nullptr);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < Relays.count(); i++) {
    const Relay* relay = Relays.getRelay(i);
    if (!relay) continue;

    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = relay->id;
    obj["customName"] = relay->customName[0] ? (const char*)relay->customName : (const char*)nullptr;
    obj["isOn"] = relay->isOn;
    obj["source"] = static_cast<int>(relay->source);
    obj["lastRunStart"] = relay->lastRunStart;
    obj["lastRunEnd"] = relay->lastRunEnd;

    if (Timers.isActive(relay->id, now)) {
      obj["timerRemaining"] = Timers.getRemainingSeconds(relay->id, now);
    }
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleRelayState() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t relayId = server.pathArg(0).toInt();
  int8_t index = Relays.findByRelayId(relayId);

  if (index < 0) {
    sendJsonError(404, "Relay not found");
    return;
  }

  const Relay* relay = Relays.getRelay(index);
  time_t now = time(nullptr);

  JsonDocument doc;
  doc["relayId"] = relayId;
  doc["customName"] = relay->customName[0] ? (const char*)relay->customName : (const char*)nullptr;
  doc["isOn"] = relay->isOn;
  doc["source"] = static_cast<int>(relay->source);

  if (Timers.isActive(relayId, now)) {
    doc["timerRemaining"] = Timers.getRemainingSeconds(relayId, now);
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleRelayUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t relayId = server.pathArg(0).toInt();
  int8_t index = Relays.findByRelayId(relayId);

  if (index < 0) {
    sendJsonError(404, "Relay not found");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (doc["customName"].is<const char*>() || doc["customName"].isNull()) {
    if (doc["customName"].isNull() || doc["customName"].as<const char*>()[0] == '\0') {
      Relays.setCustomName(index, nullptr);
    } else {
      Relays.setCustomName(index, doc["customName"].as<const char*>());
    }
  }

  sendJsonResponse(200, "Relay updated");
}

void handleRelayOn() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t relayId = server.pathArg(0).toInt();
  int8_t index = Relays.findByRelayId(relayId);

  if (index < 0) {
    sendJsonError(404, "Relay not found");
    return;
  }

  Relays.setState(index, true, RelaySource::MANUAL);
  Mqtt.publishRelayState(relayId);
  char d[32];
  snprintf(d, sizeof(d), "{\"relayId\":%d}", relayId);
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::RELAY_ON, d);
  sendJsonResponse(200, "ok");
}

void handleRelayOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  uint8_t relayId = server.pathArg(0).toInt();
  int8_t index = Relays.findByRelayId(relayId);

  if (index < 0) {
    sendJsonError(404, "Relay not found");
    return;
  }

  Timers.abort(relayId);
  Relays.setState(index, false, RelaySource::NONE);
  Mqtt.publishRelayState(relayId);
  char d[32];
  snprintf(d, sizeof(d), "{\"relayId\":%d}", relayId);
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::RELAY_OFF, d);
  sendJsonResponse(200, "ok");
}

void handleRelaysAllOff() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  Timers.abortAll();
  Relays.allOff();
  Mqtt.publishAllRelayStates();
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::RELAYS_ALL_OFF);
  sendJsonResponse(200, "ok");
}
