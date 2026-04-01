/**
 * DripDrop Configuration
 * 
 * This file contains all configurable parameters for the DripDrop irrigation system.
 * 
 * For local/private settings (WiFi credentials, etc.), create a file called
 * 'config_local.h' with your overrides. It will be included if it exists.
 */

#ifndef DRIPDROP_CONFIG_H
#define DRIPDROP_CONFIG_H

#include <Arduino.h>

// =============================================================================
// WiFi Configuration
// =============================================================================

// Primary WiFi network credentials
#ifndef WIFI_SSID
  #define WIFI_SSID "xxxxxxxxx"
#endif

#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "********"
#endif

// Access Point (fallback) credentials
#ifndef AP_SSID
  #define AP_SSID "DripDrop"
#endif

#ifndef AP_PASSWORD
  #define AP_PASSWORD "dripdrop123"  // Min 8 characters for WPA2
#endif

// WiFi connection settings
constexpr uint8_t WIFI_CONNECT_TIMEOUT_SEC = 30;
constexpr uint8_t WIFI_RETRY_DELAY_SEC = 10;
constexpr uint8_t WIFI_MAX_RETRIES = 3;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;  // Check every 30 seconds

// =============================================================================
// Network Configuration
// =============================================================================

// mDNS hostname (accessible as hostname.local)
constexpr const char* MDNS_HOSTNAME = "dripdrop";

// Web server port
constexpr uint16_t HTTP_PORT = 80;

// =============================================================================
// API Authentication (optional)
// =============================================================================

// Set to true to require API key for all requests
#ifndef API_AUTH_ENABLED
  #define API_AUTH_ENABLED false
#endif

// API key - change this to a secure random string
#ifndef API_KEY
  #define API_KEY "change-me-to-a-secure-key"
#endif

// Header name for API key
constexpr const char* API_KEY_HEADER = "X-API-Key";

// =============================================================================
// NTP Time Configuration
// =============================================================================

// NTP servers (primary and backup)
constexpr const char* NTP_SERVER_PRIMARY = "pool.ntp.org";
constexpr const char* NTP_SERVER_SECONDARY = "time.nist.gov";

// Timezone offset from UTC in seconds (e.g., CET = 3600, CEST = 7200)
#ifndef TIMEZONE_OFFSET_SEC
  #define TIMEZONE_OFFSET_SEC 3600  // UTC+1 (Central European Time)
#endif

// Daylight saving time offset in seconds (usually 3600 for 1 hour)
#ifndef DST_OFFSET_SEC
  #define DST_OFFSET_SEC 3600
#endif

// NTP sync interval in milliseconds (default: 1 hour)
constexpr unsigned long NTP_SYNC_INTERVAL_MS = 3600000UL;

// =============================================================================
// Hardware Configuration
// =============================================================================

// Number of valves supported
constexpr uint8_t NUM_VALVES = 4;

// Valve GPIO pins (active LOW - relay typically pulls LOW to activate)
// Using D1, D5, D6, D7 on NodeMCU/Wemos D1 Mini (D4 avoided - shared with onboard LED)
constexpr uint8_t VALVE_PINS[NUM_VALVES] = {D1, D5, D6, D7};

// Valve logic level (true = active HIGH, false = active LOW)
// Most relay modules are active LOW
constexpr bool VALVE_ACTIVE_HIGH = false;

// =============================================================================
// Schedule Configuration
// =============================================================================

// Maximum number of schedules (affects EEPROM usage)
constexpr uint8_t MAX_SCHEDULES = 32;

// Minimum plausible Unix timestamp (2001-09-09); used to detect valid NTP sync
constexpr time_t MIN_VALID_UNIX_TIME = 1000000000;

// EEPROM configuration
constexpr uint16_t EEPROM_SIZE = 4096;
constexpr uint16_t SCHEDULE_EEPROM_ADDR = 16;  // After header

// Maximum schedule duration in seconds (default: 4 hours)
constexpr uint16_t MAX_SCHEDULE_DURATION_SEC = 14400;

// Maximum timer duration in seconds (default: 24 hours)
constexpr uint32_t MAX_TIMER_DURATION_SEC = 86400;

// =============================================================================
// Timing Configuration
// =============================================================================

// How often to check schedules and timers (milliseconds)
constexpr unsigned long SCHEDULE_CHECK_INTERVAL_MS = 5000;

// How often to check sensors (milliseconds) - for future use
constexpr unsigned long SENSOR_CHECK_INTERVAL_MS = 60000;

// Watchdog timeout in milliseconds (8 seconds max on ESP8266)
constexpr unsigned long WATCHDOG_TIMEOUT_MS = 8000;

// =============================================================================
// Debug Configuration
// =============================================================================

// Enable/disable debug output (set to 0 for production)
#ifndef DEBUG_ENABLED
  #define DEBUG_ENABLED 1
#endif

// Serial baud rate
constexpr unsigned long SERIAL_BAUD_RATE = 115200;

// Debug macros with category support
#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
  #define DEBUG_VALVE(fmt, ...) Serial.printf("[VALVE] " fmt, ##__VA_ARGS__)
  #define DEBUG_SCHEDULE(fmt, ...) Serial.printf("[SCHED] " fmt, ##__VA_ARGS__)
  #define DD_DEBUG_WIFI(fmt, ...) Serial.printf("[WIFI] " fmt, ##__VA_ARGS__)
  #define DEBUG_API(fmt, ...) Serial.printf("[API] " fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
  #define DEBUG_VALVE(fmt, ...)
  #define DEBUG_SCHEDULE(fmt, ...)
  #define DD_DEBUG_WIFI(fmt, ...)
  #define DEBUG_API(fmt, ...)
#endif

// =============================================================================
// Version Information
// =============================================================================

#define FIRMWARE_VERSION "3.0.0-rc5"
#define FIRMWARE_NAME "DripDrop"

// =============================================================================
// Include local overrides if available
// =============================================================================

#if __has_include("config_local.h")
  #include "config_local.h"
#endif

#endif // DRIPDROP_CONFIG_H
