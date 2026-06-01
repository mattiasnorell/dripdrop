/**
 * DripDrop - Shared API Utilities
 *
 * Declares the global WebServer instance and shared state that HTTP handler
 * files need, plus utility function prototypes used across all handlers.
 */

#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <atomic>
#include "types.h"

// Global WebServer instance (defined in dripdrop.cpp)
extern WebServer server;

// Global state shared between dripdrop.cpp and handler files
extern bool apMode;
extern std::atomic<bool> ntpSynced;
extern unsigned long lastNtpSync;
extern String deviceName;
extern String wifiSsid;
extern String wifiPassword;

// Settings persistence (implemented in dripdrop.cpp)
void saveSettings();

// Shared HTTP utility functions (implemented in api_utils.cpp)
void sendJsonResponse(int code, const char* message);
void sendJsonError(int code, const char* error);
void sendCorsHeaders();
bool parseJsonBody(JsonDocument& doc);
bool checkApiAuth();
SystemStatus getSystemStatus();
