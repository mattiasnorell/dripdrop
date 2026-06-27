/**
 * DripDrop - Filesystem Upload Handler
 *
 * POST   /fs/upload?path=<path>  — uploads a single file to LittleFS.
 * DELETE /fs/dir?path=<path>     — recursively deletes a directory.
 * Used by the Makefile ota-webapp target to update webapp files without
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

void mkdirRecursive(const String& path) {
  for (int i = 1; i <= (int)path.length(); i++) {
    if (path[i] == '/' || i == (int)path.length()) {
      String seg = path.substring(0, i);
      if (seg.length() > 1 && !LittleFS.exists(seg)) {
        LittleFS.mkdir(seg);
      }
    }
  }
}

void removeDirRecursive(const String& dirPath) {
  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while ((entry = dir.openNextFile())) {
    String entryPath = dirPath + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      removeDirRecursive(entryPath);
      LittleFS.rmdir(entryPath);
    } else {
      entry.close();
      LittleFS.remove(entryPath);
    }
  }
  dir.close();
}

void handleFsDirDelete() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  String path = server.arg("path");
  if (path.isEmpty() || path == "/") {
    sendJsonError(400, "Invalid path");
    return;
  }
  if (!path.startsWith("/")) path = "/" + path;
  removeDirRecursive(path);
  LittleFS.rmdir(path);
  sendJsonResponse(200, "deleted");
}

static void listDirRecursive(const String& dirPath, JsonArray files) {
  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while ((entry = dir.openNextFile())) {
    String entryPath = dirPath + (dirPath.endsWith("/") ? "" : "/") + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      listDirRecursive(entryPath, files);
    } else {
      JsonObject obj = files.add<JsonObject>();
      obj["path"] = entryPath;
      obj["size"] = (unsigned int)entry.size();
      entry.close();
    }
  }
  dir.close();
}

void handleFsInfo() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  JsonDocument doc;
  doc["total"] = (unsigned int)LittleFS.totalBytes();
  doc["used"]  = (unsigned int)LittleFS.usedBytes();
  doc["free"]  = (unsigned int)(LittleFS.totalBytes() - LittleFS.usedBytes());
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleFsList() {
  sendCorsHeaders();
  if (!checkApiAuth()) return;
  String path = server.arg("path");
  if (path.isEmpty()) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  JsonDocument doc;
  doc["path"] = path;
  JsonArray files = doc["files"].to<JsonArray>();
  listDirRecursive(path, files);
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleFsUpload() {
  if (!checkApiAuth()) return;

  HTTPUpload& upload = server.upload();

  String path = server.arg("path");
  if (path.isEmpty()) path = "/" + upload.filename;
  if (!path.startsWith("/")) path = "/" + path;

  if (upload.status == UPLOAD_FILE_START) {
    String dir = path.substring(0, path.lastIndexOf('/'));
    if (dir.length() > 1) mkdirRecursive(dir);
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
