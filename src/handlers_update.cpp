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
#include "ota_rollback.h"
#include "display.h"

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

// Download `url` and stream it straight into the firmware partition. `md5` must be the
// 32-char hex digest from the manifest: the Update library verifies it and Update.end()
// fails on mismatch, so a corrupt/truncated download can never switch the boot
// partition. Without a digest a truncated stream would be accepted (Update.end(true)
// skips the completeness check), so flashing without one is refused outright.
static bool downloadFirmware(const String& url, const char* md5) {
  if (!md5 || strlen(md5) != 32) {
    DEBUG_PRINTLN("[UPD] refusing to flash without an md5 digest");
    return false;
  }

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

  Update.setMD5(md5);
  DEBUG_PRINTF("[UPD] firmware md5 expected: %s\n", md5);

  size_t written = Update.writeStream(*http.getStreamPtr());
  bool ok = Update.end(true);  // fails here on an MD5 mismatch
  endGet(http, secureClient);

  if (!ok || written == 0) {
    DEBUG_PRINTF("[UPD] firmware flash failed: %s\n", Update.errorString());
    return false;
  }
  DEBUG_PRINTF("[UPD] firmware flashed: %u bytes\n", (unsigned)written);
  return true;
}

// Swap the freshly-staged webapp over the live one. The live dir is parked as
// WEBAPP_OLD_DIR (not deleted) so otaBootCheck() can restore the matching UI if the new
// firmware is rolled back; otaMarkUpdateValid() deletes the parked copy once the update
// sticks. Both renames are local metadata operations, so the window with no live webapp
// is milliseconds. On any failure the live webapp is left (or put back) in place.
static bool commitWebappStaging() {
  if (!LittleFS.exists(WEBAPP_STAGING_DIR)) {
    DEBUG_PRINTLN("[UPD] no staged webapp to commit");
    return false;
  }
  removeTree(WEBAPP_OLD_DIR);  // stale backup from a previously-aborted update
  if (LittleFS.exists(WEBAPP_DIR) && !LittleFS.rename(WEBAPP_DIR, WEBAPP_OLD_DIR)) {
    DEBUG_PRINTLN("[UPD] webapp backup rename failed");
    return false;
  }
  if (!LittleFS.rename(WEBAPP_STAGING_DIR, WEBAPP_DIR)) {
    LittleFS.rename(WEBAPP_OLD_DIR, WEBAPP_DIR);  // put the old webapp back
    DEBUG_PRINTLN("[UPD] webapp staging rename failed");
    return false;
  }
  DEBUG_PRINTLN("[UPD] webapp swapped in");
  return true;
}

// Common exit for failures during the blocking phase: discard the staged webapp, re-arm
// the watchdog, and report to both the LCD and the API caller. The LCD notice goes
// through showOverride() so Display.update() keeps it on screen once loop() resumes —
// a direct showMessage() would be repainted by the normal status screen within a second.
static void failUpdate(const char* lcdDetail, const char* apiMsg) {
  removeTree(WEBAPP_STAGING_DIR);
  esp_task_wdt_add(NULL);
  Display.showOverride("Update failed", lcdDetail, "", "", 30);
  sendJsonError(502, apiMsg);
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
  const char* fwMd5  = doc["firmware"]["md5"] | "";
  if (strlen(remoteVersion) == 0) {
    sendJsonError(502, "Manifest missing firmware.version");
    return;
  }
  // create-manifest.sh always emits the digest, so a missing/malformed one means a
  // stale or foreign manifest — refuse rather than flash without an integrity check.
  if (strlen(fwMd5) != 32) {
    sendJsonError(502, "Manifest missing firmware.md5");
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

  // 3. Fail fast if LittleFS can't hold the staged webapp copy alongside the live one.
  //    webappBytes from the manifest is a lower bound (per-file FS overhead comes on
  //    top) — the per-file write failure below still catches what this misses.
  const size_t webappBytes = doc["webappBytes"] | (size_t)0;
  if (webappBytes > 0 && LittleFS.totalBytes() - LittleFS.usedBytes() < webappBytes) {
    String msg = "Not enough filesystem space for webapp (total " + String(LittleFS.totalBytes()) +
                 " B, used " + String(LittleFS.usedBytes()) + " B, need " + String(webappBytes) + " B)";
    sendJsonError(507, msg.c_str());
    return;
  }

  // Long blocking operation from here — disable the task watchdog. Progress goes to the
  // LCD via showMessage(): loop()/Display.update() won't run while this handler blocks,
  // so the panel must be written synchronously.
  esp_task_wdt_delete(NULL);

  // 4. Download the new webapp into a STAGING dir (config at the root is untouched, and
  //    the live /webapp is left intact until every file has been fetched). Clear any
  //    leftover staging from a previously-aborted run first.
  Display.showMessage("Update in progress", "Download UI", "Step 1/3", "");
  removeTree(WEBAPP_STAGING_DIR);
  size_t staged = 0;
  for (JsonVariant v : doc["webapp"].as<JsonArray>()) {
    const char* rel = v.as<const char*>();
    if (!rel || strlen(rel) == 0) continue;
    String relPath = rel;
    if (relPath.startsWith("/")) relPath = relPath.substring(1);
    if (!downloadToFile(base + "/webapp/" + relPath,
                        String(WEBAPP_STAGING_DIR) + "/" + relPath)) {
      failUpdate("Can't update UI", "Webapp download failed");
      return;
    }
    staged++;
  }
  if (staged == 0) {
    // A manifest with no webapp files is malformed (create-manifest.sh always emits the
    // list) — bail before flashing rather than commit an empty UI later.
    failUpdate("Manifest has no UI", "Manifest missing webapp files");
    return;
  }

  // 5. Download + flash the firmware into the INACTIVE partition (MD5-verified). On
  //    failure nothing has been committed: the live webapp and the running firmware are
  //    both untouched, so we just discard the staged webapp and bail.
  Display.showMessage("Update in progress", "Download firmware", "Step 2/3", "");
  if (!downloadFirmware(base + "/" + fwPath, fwMd5)) {
    failUpdate("Can't update firmware", "Firmware update failed");
    return;
  }

  // 6. Firmware is verified and staged — now commit the webapp by swapping staging over
  //    the live dir, parking the old one for a potential rollback. (Ordering: everything
  //    downloaded/verified before either is committed.)
  if (!commitWebappStaging()) {
    // The live webapp is still (or back) in place. The firmware is already flashed and
    // will boot next; proceed to reboot rather than leave it half-applied — the
    // pending-verify guard below still protects a bad firmware.
    removeTree(WEBAPP_STAGING_DIR);
    DEBUG_PRINTLN("[UPD] warning: webapp swap failed, continuing with firmware update");
  }

  // 7. Arm the pending-verify guard: the new firmware must reach otaMarkUpdateValid() on
  //    boot, or otaBootCheck() rolls back to the current (known-good) firmware slot and
  //    restores the webapp parked in WEBAPP_OLD_DIR.
  Display.showMessage("Update in progress", "Verifying firmware", "Step 3/3", "");
  otaArmPendingVerify();

  // 8. Success — show confirmation on the LCD (the panel keeps displaying it through the
  //    restart), flush the response, then reboot into the new firmware.
  Display.showMessage("Update in progress", "Update successful", "Rebooting...", "");
  sendJsonResponse(200, "updating");
  server.client().stop();
  delay(500);
  ESP.restart();
}
