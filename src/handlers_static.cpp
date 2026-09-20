/**
 * DripDrop - Static File & SPA Serving
 *
 * Serves the React dashboard from LittleFS with gzip support.
 * Falls back to the PROGMEM provisioning form if no index.html exists on LittleFS.
 */

#include "api_utils.h"
#include "AppHtml.h"
#include "config.h"
#include <LittleFS.h>

static String getContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

// Try .gz variant first, then uncompressed. Returns true if the file was sent.
// Files are stored under WEBAPP_DIR on LittleFS; path is the request URI.
static bool serveFile(const String& path) {
  String fsPath = String(WEBAPP_DIR) + path;
  String gzPath = fsPath + ".gz";
  /*if (LittleFS.exists(gzPath)) {
    File f = LittleFS.open(gzPath, "r");
    if (f) {
      server.sendHeader("Content-Encoding", "gzip");
      server.sendHeader("Cache-Control",
        path == "/index.html" ? "no-cache" : "public, max-age=31536000, immutable");
      server.streamFile(f, getContentType(path));
      f.close();
      return true;
    }
  }*/
  if (LittleFS.exists(fsPath)) {
    File f = LittleFS.open(fsPath, "r");
    if (f) {
      server.sendHeader("Cache-Control",
        path == "/index.html" ? "no-cache" : "public, max-age=31536000, immutable");
      server.streamFile(f, getContentType(path));
      f.close();
      return true;
    }
  }
  return false;
}

// GET / — try LittleFS index.html, fall back to PROGMEM provisioning form
void handleRoot() {
  sendCorsHeaders();
  if (!serveFile("/index.html")) {
    server.send_P(200, "text/html", APP_HTML);
  }
}

// Registered as server.onNotFound() — serves assets and SPA fallback
void handleStaticFile() {
  sendCorsHeaders();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  String path = server.uri();
  if (serveFile(path)) return;

  // Known asset extensions that don't exist → 404 (not a SPA route)
  if (path.startsWith("/assets/") ||
      path.endsWith(".js")    || path.endsWith(".css")   ||
      path.endsWith(".png")   || path.endsWith(".ico")   ||
      path.endsWith(".svg")   || path.endsWith(".woff2")) {
    sendJsonError(404, "Not Found");
    return;
  }

  // SPA fallback: serve index.html so the client router handles the path
  if (serveFile("/index.html")) return;

  // No index.html yet (uploadfs not run) → PROGMEM fallback
  server.send_P(200, "text/html", APP_HTML);
}
