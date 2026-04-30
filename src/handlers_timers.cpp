/**
 * DripDrop - Timer HTTP Handlers
 *
 * Handles: /timers, /valves/{id}/timer (POST + DELETE)
 */

#include "api_utils.h"
#include "timers.h"
#include "valves.h"
#include "config.h"
#include <time.h>

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
