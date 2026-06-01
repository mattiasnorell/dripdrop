/**
 * DripDrop - Config Export/Import Handlers
 *
 * GET  /config/export  — returns all config files bundled as one JSON object.
 * POST /config/import  — restores config files from a bundled JSON, then reboots.
 */

#include "api_utils.h"
#include "config.h"
#include "scenarios.h"
#include "modules.h"
#include <LittleFS.h>

static void loadFileIntoDoc(JsonDocument& out, const char* key, const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    out[key] = nullptr;
    return;
  }
  JsonDocument tmp;
  deserializeJson(tmp, f);
  f.close();
  out[key].set(tmp.as<JsonVariant>());
}

void handleConfigExport() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  loadFileIntoDoc(doc, "settings",   SETTINGS_FILE);
  loadFileIntoDoc(doc, "scenarios",  SCENARIOS_FILE);
  loadFileIntoDoc(doc, "modules",    MODULES_FILE);
  loadFileIntoDoc(doc, "valveNames", VALVE_NAMES_FILE);

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleConfigImport() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  JsonDocument doc;
  if (!parseJsonBody(doc)) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  struct { const char* key; const char* path; } files[] = {
    {"settings",   SETTINGS_FILE},
    {"scenarios",  SCENARIOS_FILE},
    {"modules",    MODULES_FILE},
    {"valveNames", VALVE_NAMES_FILE},
  };

  for (auto& entry : files) {
    if (doc[entry.key].isNull()) continue;
    File f = LittleFS.open(entry.path, "w");
    if (!f) continue;
    serializeJson(doc[entry.key], f);
    f.close();
  }

  sendJsonResponse(200, "Rebooting...");
  delay(500);
  ESP.restart();
}
