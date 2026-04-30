/**
 * DripDrop - Scenario HTTP Handlers
 *
 * Handles: /scenarios (GET + POST), /scenarios/{id} (POST + DELETE)
 */

#include "api_utils.h"
#include "scenarios.h"
#include "mqtt.h"

void handleScenarioList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String output;
  Scenarios.serialize(output);
  server.send(200, "application/json", output);
}

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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SCENARIO_ADD, d);

  JsonDocument response;
  response["message"] = "ok";
  response["id"] = newId;

  String output;
  serializeJson(response, output);
  server.send(200, "application/json", output);
}

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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SCENARIO_UPDATE, d);
  sendJsonResponse(200, "ok");
}

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
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SCENARIO_DELETE, d);
  sendJsonResponse(200, "ok");
}
