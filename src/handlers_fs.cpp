/**
 * DripDrop - Filesystem Upload Handler
 *
 * POST /fs/upload?path=<path>  — uploads a single file to LittleFS.
 * Used by the Makefile ota-fs target to update webapp files without
 * touching config files (settings.json, scenarios.json, etc.).
 */

#include "api_utils.h"
#include "config.h"
#include <LittleFS.h>

static File fsUploadFile;

void handleFsUploadComplete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  sendJsonResponse(200, "uploaded");
}

void handleFsUpload() {
  if (!checkApiAuth()) return;

  HTTPUpload& upload = server.upload();

  String path = server.arg("path");
  if (path.isEmpty()) path = "/" + upload.filename;
  if (!path.startsWith("/")) path = "/" + path;

  if (upload.status == UPLOAD_FILE_START) {
    String dir = path.substring(0, path.lastIndexOf('/'));
    if (dir.length() > 1 && !LittleFS.exists(dir)) {
      LittleFS.mkdir(dir);
    }
    if (LittleFS.exists(path)) LittleFS.remove(path);
    fsUploadFile = LittleFS.open(path, "w");
    DEBUG_PRINTF("[FS] Upload start: %s\n", path.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      DEBUG_PRINTF("[FS] Upload done: %s (%u bytes)\n", path.c_str(), upload.totalSize);
    }
  }
}
