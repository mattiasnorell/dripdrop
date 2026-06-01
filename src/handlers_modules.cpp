/**
 * DripDrop - Module HTTP Handlers
 *
 * Handles: /modules, /modules/scan, /modules/{uid}/register,
 *          /modules/{uid}, /modules/{uid}/reading (DELETE + GET)
 */

#include "api_utils.h"
#include "modules.h"

void handleModuleList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  String output;
  Modules.serializeRegistered(output);
  server.send(200, "application/json", output);
}

void handleModuleScan() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  Modules.scanModules();
  String output;
  Modules.serializeScan(output);
  server.send(200, "application/json", output);
}

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
