/**
 * DripDrop - OTA Update Handlers
 *
 * POST /ota/upload     — streams a firmware binary into flash and reboots.
 * POST /ota/upload-fs  — streams a LittleFS image into the FS partition and reboots.
 * Uses the ESP32 built-in Update library via the existing WebServer.
 */

#include <Update.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "api_utils.h"
#include "config.h"

// --- Firmware ---

void handleOtaUploadComplete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  if (Update.hasError()) {
    esp_task_wdt_add(NULL);
    sendJsonResponse(500, "update failed");
  } else {
    sendJsonResponse(200, "ok");
    server.client().stop();  // flush TCP before chip resets
    delay(500);
    ESP.restart();
  }
}

void handleOtaUpload() {
  if (!checkApiAuth()) return;

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    esp_task_wdt_delete(NULL);
    DEBUG_PRINTLN(F("[OTA] Firmware update started"));
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      DEBUG_PRINTF("[OTA] begin failed: %s\n", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      DEBUG_PRINTF("[OTA] write failed: %s\n", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      DEBUG_PRINTF("[OTA] Firmware success: %u bytes\n", upload.totalSize);
    } else {
      DEBUG_PRINTF("[OTA] end failed: %s\n", Update.errorString());
    }
  }
}

// --- Filesystem image ---

void handleOtaFsUploadComplete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  if (Update.hasError()) {
    LittleFS.begin(true);   // remount so the device stays usable
    esp_task_wdt_add(NULL);
    sendJsonResponse(500, "update failed");
  } else {
    sendJsonResponse(200, "ok");
    server.client().stop();  // flush TCP before chip resets
    delay(500);
    ESP.restart();
  }
}

void handleOtaFsUpload() {
  if (!checkApiAuth()) return;

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    esp_task_wdt_delete(NULL);
    DEBUG_PRINTLN(F("[OTA] FS update started"));
    LittleFS.end();  // unmount before writing to the partition
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
      DEBUG_PRINTF("[OTA] FS begin failed: %s\n", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      DEBUG_PRINTF("[OTA] FS write failed: %s\n", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      DEBUG_PRINTF("[OTA] FS success: %u bytes\n", upload.totalSize);
    } else {
      DEBUG_PRINTF("[OTA] FS end failed: %s\n", Update.errorString());
    }
  }
}
