/**
 * DripDrop - HTTP Route Registration
 *
 * Wires every URL to its handler on the global WebServer. The handler bodies
 * live in the handlers_*.cpp files; this file only owns the routing table.
 */

#include "api_utils.h"      // brings in <WebServer.h> + extern WebServer server
#include <uri/UriBraces.h>  // path-parameter patterns, e.g. UriBraces("/valves/{}")
#include "routes.h"

// =============================================================================
// Forward Declarations — HTTP Handlers (defined in handlers_*.cpp)
// =============================================================================

void handleRoot();
void handleStaticFile();
void handleFsInfo();
void handleFsList();
void handleFsDirDelete();
void handleFsUploadComplete();
void handleFsUpload();
void handleOtaUploadComplete();
void handleOtaUpload();
void handleOtaFsUploadComplete();
void handleOtaFsUpload();
void handleSystemStatus();
void handleSystemIp();
void handleSystemPing();
void handleSystemTime();
void handleSystemTimePost();
void handleSystemReboot();
void handleSystemNameGet();
void handleSystemNamePost();
void handleSystemMqttGet();
void handleSystemMqttPost();
void handleSystemWifiGet();
void handleSystemWifiPost();
void handleValveList();
void handleValveState();
void handleValveUpdate();
void handleValveOn();
void handleValveOff();
void handleValvesAllOff();
void handleTimerGet();
void handleTimerPost();
void handleTimerAbort();
void handleScenarioList();
void handleScenarioAdd();
void handleScenarioUpdate();
void handleScenarioDelete();
void handleScenarioRun();
void handleModuleList();
void handleModuleScan();
void handleModuleRegister();
void handleModuleUpdate();
void handleModuleRemove();
void handleModuleReading();
void handleModuleCommand();
void handleConfigExport();
void handleConfigImport();

// =============================================================================
// HTTP Route Setup
// =============================================================================

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);

  // System
  server.on("/system/status", HTTP_GET, handleSystemStatus);
  server.on("/system/ip", HTTP_GET, handleSystemIp);
  server.on("/system/ping", HTTP_GET, handleSystemPing);
  server.on("/system/time", HTTP_GET, handleSystemTime);
  server.on("/system/time", HTTP_POST, handleSystemTimePost);
  server.on("/system/reboot", HTTP_POST, handleSystemReboot);
  server.on("/system/name", HTTP_GET, handleSystemNameGet);
  server.on("/system/name", HTTP_POST, handleSystemNamePost);
  server.on("/system/mqtt", HTTP_GET, handleSystemMqttGet);
  server.on("/system/mqtt", HTTP_POST, handleSystemMqttPost);
  server.on("/system/wifi", HTTP_GET, handleSystemWifiGet);
  server.on("/system/wifi", HTTP_POST, handleSystemWifiPost);

  // Valves
  server.on("/valves", HTTP_GET, handleValveList);
  server.on("/valves/off", HTTP_POST, handleValvesAllOff);
  server.on(UriBraces("/valves/{}/state"), HTTP_GET, handleValveState);
  server.on(UriBraces("/valves/{}"), HTTP_POST, handleValveUpdate);
  server.on(UriBraces("/valves/{}/on"), HTTP_POST, handleValveOn);
  server.on(UriBraces("/valves/{}/off"), HTTP_POST, handleValveOff);

  // Timers
  server.on("/timers", HTTP_GET, handleTimerGet);
  server.on(UriBraces("/valves/{}/timer"), HTTP_POST, handleTimerPost);
  server.on(UriBraces("/valves/{}/timer"), HTTP_DELETE, handleTimerAbort);

  // Scenarios — register /scenarios/{}/run before /scenarios/{} (longer pattern first)
  server.on("/scenarios", HTTP_GET, handleScenarioList);
  server.on("/scenarios", HTTP_POST, handleScenarioAdd);
  server.on(UriBraces("/scenarios/{}/run"), HTTP_POST, handleScenarioRun);
  server.on(UriBraces("/scenarios/{}"), HTTP_POST, handleScenarioUpdate);
  server.on(UriBraces("/scenarios/{}"), HTTP_DELETE, handleScenarioDelete);

  // Modules — register /modules/{}/register before /modules/{} so the longer
  // pattern is tested first by the router
  server.on("/modules", HTTP_GET, handleModuleList);
  server.on("/modules/scan", HTTP_POST, handleModuleScan);
  server.on(UriBraces("/modules/{}/register"), HTTP_POST, handleModuleRegister);
  server.on(UriBraces("/modules/{}/reading"), HTTP_GET, handleModuleReading);
  server.on(UriBraces("/modules/{}/command"), HTTP_POST, handleModuleCommand);
  server.on(UriBraces("/modules/{}"), HTTP_POST, handleModuleUpdate);
  server.on(UriBraces("/modules/{}"), HTTP_DELETE, handleModuleRemove);

  // Config backup/restore
  server.on("/config/export", HTTP_GET,  handleConfigExport);
  server.on("/config/import", HTTP_POST, handleConfigImport);

  // Filesystem inspection
  server.on("/fs/info", HTTP_GET, handleFsInfo);
  server.on("/fs/list", HTTP_GET, handleFsList);
  // Filesystem: clear a directory (used by make ota-webapp before re-upload)
  server.on("/fs/dir", HTTP_DELETE, handleFsDirDelete);
  // Filesystem: upload a single file (used by make ota-webapp)
  server.on("/fs/upload", HTTP_POST, handleFsUploadComplete, handleFsUpload);

  // OTA updates (used by make ota-firmware / ota-fs)
  server.on("/ota/upload",    HTTP_POST, handleOtaUploadComplete,   handleOtaUpload);
  server.on("/ota/upload-fs", HTTP_POST, handleOtaFsUploadComplete, handleOtaFsUpload);

  // Static file serving + SPA fallback (registered last)
  server.onNotFound(handleStaticFile);
}
