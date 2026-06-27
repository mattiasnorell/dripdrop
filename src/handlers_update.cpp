/**
 * DripDrop - Self-Update Handler (OTA pull)
 *
 * POST /system/update — the device pulls a manifest from UPDATE_BASE_URL, and if
 * it advertises a newer firmware version, downloads the webapp files (file by
 * file, preserving root-level config) and then flashes the firmware and reboots.
 *
 * Mirrors the existing push-based OTA (handlers_ota.cpp) and reuses the
 * HTTPS-with-setInsecure pattern from scenarios.cpp.
 */

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "api_utils.h"
#include "config.h"

// Single source of truth for the update source. Swap this for a settings-backed
// value later without touching the download/flash logic below.
static String getUpdateBaseUrl() {
  return String(UPDATE_BASE_URL);
}

// Begin a GET on `http`. For https URLs a TLS client is allocated into
// *secureClient (the caller must delete it after http.end() via endGet()).
// Returns the HTTP status code (negative on connection error).
static int beginGet(HTTPClient& http, WiFiClientSecure*& secureClient, const String& url) {
  secureClient = nullptr;
  http.setTimeout(UPDATE_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub release assets 302 to a CDN
  if (url.startsWith("https://")) {
    // HTTPS: cert verification skipped — standard for embedded devices
    secureClient = new WiFiClientSecure;
    secureClient->setInsecure();
    if (!http.begin(*secureClient, url)) return -1;
  } else {
    if (!http.begin(url)) return -1;
  }
  return http.GET();
}

static void endGet(HTTPClient& http, WiFiClientSecure* secureClient) {
  http.end();
  if (secureClient) delete secureClient;
}

// Download `url` into `fsPath` on LittleFS, creating parent dirs as needed.
static bool downloadToFile(const String& url, const String& fsPath) {
  HTTPClient http;
  WiFiClientSecure* secureClient;
  int code = beginGet(http, secureClient, url);
  if (code != HTTP_CODE_OK) {
    DEBUG_PRINTF("[UPD] GET %s -> %d\n", url.c_str(), code);
    endGet(http, secureClient);
    return false;
  }

  String dir = fsPath.substring(0, fsPath.lastIndexOf('/'));
  if (dir.length() > 1) mkdirRecursive(dir);
  if (LittleFS.exists(fsPath)) LittleFS.remove(fsPath);

  File f = LittleFS.open(fsPath, "w");
  if (!f) {
    DEBUG_PRINTF("[UPD] open %s failed\n", fsPath.c_str());
    endGet(http, secureClient);
    return false;
  }

  int written = http.writeToStream(&f);
  f.close();
  endGet(http, secureClient);

  if (written < 0) {
    DEBUG_PRINTF("[UPD] write %s failed: %d\n", fsPath.c_str(), written);
    return false;
  }
  DEBUG_PRINTF("[UPD] %s (%d bytes)\n", fsPath.c_str(), written);
  return true;
}

// Download `url` and stream it straight into the firmware partition.
static bool downloadFirmware(const String& url) {
  HTTPClient http;
  WiFiClientSecure* secureClient;
  int code = beginGet(http, secureClient, url);
  if (code != HTTP_CODE_OK) {
    DEBUG_PRINTF("[UPD] firmware GET -> %d\n", code);
    endGet(http, secureClient);
    return false;
  }

  int len = http.getSize();
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    DEBUG_PRINTF("[UPD] Update.begin failed: %s\n", Update.errorString());
    endGet(http, secureClient);
    return false;
  }

  size_t written = Update.writeStream(*http.getStreamPtr());
  bool ok = Update.end(true);
  endGet(http, secureClient);

  if (!ok || written == 0) {
    DEBUG_PRINTF("[UPD] firmware flash failed: %s\n", Update.errorString());
    return false;
  }
  DEBUG_PRINTF("[UPD] firmware flashed: %u bytes\n", (unsigned)written);
  return true;
}

// Numeric dotted-version compare: returns true if `remote` > `current`.
static bool isNewerVersion(const char* remote, const char* current) {
  int r[3] = {0, 0, 0}, c[3] = {0, 0, 0};
  sscanf(remote, "%d.%d.%d", &r[0], &r[1], &r[2]);
  sscanf(current, "%d.%d.%d", &c[0], &c[1], &c[2]);
  for (int i = 0; i < 3; i++) {
    if (r[i] != c[i]) return r[i] > c[i];
  }
  return false;
}

void handleSystemUpdate() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;

  const String base = getUpdateBaseUrl();

  // 1. Fetch + parse the manifest
  HTTPClient http;
  WiFiClientSecure* secureClient;
  int code = beginGet(http, secureClient, base + "/manifest.json");
  if (code != HTTP_CODE_OK) {
    DEBUG_PRINTF("[UPD] manifest GET -> %d\n", code);
    endGet(http, secureClient);
    sendJsonError(502, "Failed to fetch manifest");
    return;
  }
  String body = http.getString();
  endGet(http, secureClient);

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    sendJsonError(502, "Invalid manifest");
    return;
  }

  const char* remoteVersion = doc["firmware"]["version"] | "";
  const char* fwPath = doc["firmware"]["path"] | "firmware.bin";
  if (strlen(remoteVersion) == 0) {
    sendJsonError(502, "Manifest missing firmware.version");
    return;
  }

  // 2. Only update if strictly newer
  if (!isNewerVersion(remoteVersion, FIRMWARE_VERSION)) {
    JsonDocument resp;
    resp["status"]    = "up-to-date";
    resp["current"]   = FIRMWARE_VERSION;
    resp["available"] = remoteVersion;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
    return;
  }

  // Long blocking operation from here — disable the task watchdog
  esp_task_wdt_delete(NULL);

  // 3. Refresh the webapp file-by-file (root-level config is left untouched)
  removeDirRecursive("/webapp");
  for (JsonVariant v : doc["webapp"].as<JsonArray>()) {
    const char* rel = v.as<const char*>();
    if (!rel || strlen(rel) == 0) continue;
    String relPath = rel;
    if (relPath.startsWith("/")) relPath = relPath.substring(1);
    if (!downloadToFile(base + "/webapp/" + relPath, String("/webapp/") + relPath)) {
      esp_task_wdt_add(NULL);
      sendJsonError(502, "Webapp download failed");
      return;
    }
  }

  // 4. Flash firmware last (reboots on success)
  if (!downloadFirmware(base + "/" + fwPath)) {
    esp_task_wdt_add(NULL);
    sendJsonError(502, "Firmware update failed");
    return;
  }

  // 5. Success — flush the response, then reboot into the new firmware
  sendJsonResponse(200, "updating");
  server.client().stop();
  delay(500);
  ESP.restart();
}
