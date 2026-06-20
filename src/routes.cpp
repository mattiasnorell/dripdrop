/**
 * DripDrop - HTTP Route Registration
 *
 * Wires every URL to its handler on the global WebServer. The handler bodies
 * live in the handlers_*.cpp files; this file only owns the routing table.
 */

#include "api_utils.h"      // brings in <WebServer.h> + extern WebServer server
#include <uri/UriBraces.h>  // path-parameter patterns, e.g. UriBraces("/api/v1/relays/{}")
#include "routes.h"

// Every API endpoint lives under this versioned prefix. API "/relays" expands
// (via string-literal concatenation) to "/api/v1/relays".
#define API "/api/v1"

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
void handleRelayList();
void handleRelayState();
void handleRelayUpdate();
void handleRelayOn();
void handleRelayOff();
void handleRelaysAllOff();
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
  server.on(API "/system/status", HTTP_GET, handleSystemStatus);
  server.on(API "/system/ip", HTTP_GET, handleSystemIp);
  server.on(API "/system/ping", HTTP_GET, handleSystemPing);
  server.on(API "/system/time", HTTP_GET, handleSystemTime);
  server.on(API "/system/time", HTTP_POST, handleSystemTimePost);
  server.on(API "/system/reboot", HTTP_POST, handleSystemReboot);
  server.on(API "/system/name", HTTP_GET, handleSystemNameGet);
  server.on(API "/system/name", HTTP_POST, handleSystemNamePost);
  server.on(API "/system/mqtt", HTTP_GET, handleSystemMqttGet);
  server.on(API "/system/mqtt", HTTP_POST, handleSystemMqttPost);
  server.on(API "/system/wifi", HTTP_GET, handleSystemWifiGet);
  server.on(API "/system/wifi", HTTP_POST, handleSystemWifiPost);

  // Relays
  server.on(API "/relays", HTTP_GET, handleRelayList);
  server.on(API "/relays/off", HTTP_POST, handleRelaysAllOff);
  server.on(UriBraces(API "/relays/{}/state"), HTTP_GET, handleRelayState);
  server.on(UriBraces(API "/relays/{}"), HTTP_POST, handleRelayUpdate);
  server.on(UriBraces(API "/relays/{}/on"), HTTP_POST, handleRelayOn);
  server.on(UriBraces(API "/relays/{}/off"), HTTP_POST, handleRelayOff);

  // Timers
  server.on(API "/timers", HTTP_GET, handleTimerGet);
  server.on(UriBraces(API "/relays/{}/timer"), HTTP_POST, handleTimerPost);
  server.on(UriBraces(API "/relays/{}/timer"), HTTP_DELETE, handleTimerAbort);

  // Scenarios — register /scenarios/{}/run before /scenarios/{} (longer pattern first)
  server.on(API "/scenarios", HTTP_GET, handleScenarioList);
  server.on(API "/scenarios", HTTP_POST, handleScenarioAdd);
  server.on(UriBraces(API "/scenarios/{}/run"), HTTP_POST, handleScenarioRun);
  server.on(UriBraces(API "/scenarios/{}"), HTTP_POST, handleScenarioUpdate);
  server.on(UriBraces(API "/scenarios/{}"), HTTP_DELETE, handleScenarioDelete);

  // Modules — register /modules/{}/register before /modules/{} so the longer
  // pattern is tested first by the router
  server.on(API "/modules", HTTP_GET, handleModuleList);
  server.on(API "/modules/scan", HTTP_POST, handleModuleScan);
  server.on(UriBraces(API "/modules/{}/register"), HTTP_POST, handleModuleRegister);
  server.on(UriBraces(API "/modules/{}/reading"), HTTP_GET, handleModuleReading);
  server.on(UriBraces(API "/modules/{}/command"), HTTP_POST, handleModuleCommand);
  server.on(UriBraces(API "/modules/{}"), HTTP_POST, handleModuleUpdate);
  server.on(UriBraces(API "/modules/{}"), HTTP_DELETE, handleModuleRemove);

  // Config backup/restore
  server.on(API "/config/export", HTTP_GET,  handleConfigExport);
  server.on(API "/config/import", HTTP_POST, handleConfigImport);

  // Filesystem inspection
  server.on(API "/fs/info", HTTP_GET, handleFsInfo);
  server.on(API "/fs/list", HTTP_GET, handleFsList);
  // Filesystem: clear a directory (used by make ota-webapp before re-upload)
  server.on(API "/fs/dir", HTTP_DELETE, handleFsDirDelete);
  // Filesystem: upload a single file (used by make ota-webapp)
  server.on(API "/fs/upload", HTTP_POST, handleFsUploadComplete, handleFsUpload);

  // OTA updates (used by make ota-firmware / ota-fs)
  server.on(API "/ota/upload",    HTTP_POST, handleOtaUploadComplete,   handleOtaUpload);
  server.on(API "/ota/upload-fs", HTTP_POST, handleOtaFsUploadComplete, handleOtaFsUpload);

  // Static file serving + SPA fallback (registered last)
  server.onNotFound(handleStaticFile);
}
