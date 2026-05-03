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
  #define WIFI_SSID "WajFaj"
#endif

#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "7357_m4n0"
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
// MQTT Configuration
// =============================================================================

// MQTT broker address and port
#ifndef MQTT_SERVER
  #define MQTT_SERVER ""
#endif

#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif

// MQTT credentials (optional)
#ifndef MQTT_USER
  #define MQTT_USER ""
#endif

#ifndef MQTT_PASSWORD
  #define MQTT_PASSWORD ""
#endif

// Non-blocking reconnect interval
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 15000;

// Periodic full-state publish interval
constexpr unsigned long MQTT_STATE_INTERVAL_MS = 60000;

// PubSubClient buffer size (needs room for JSON payloads)
constexpr uint16_t MQTT_BUFFER_SIZE = 512;

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
// Using GPIO 25, 26, 27, 32 — safe on all ESP32 DevKit variants.
// Strapping pins to avoid: 0 (boot mode), 2 (boot/LED), 5, 12, 15.
// Also avoid 6-11 (SPI flash), 34-39 (input-only, no output driver).
constexpr uint8_t VALVE_PINS[NUM_VALVES] = {25, 26, 27, 32};

// Valve logic level (true = active HIGH, false = active LOW)
// Most relay modules are active LOW
constexpr bool VALVE_ACTIVE_HIGH = false;

// =============================================================================
// Scenario Configuration
// =============================================================================

// Maximum number of scenarios
constexpr uint8_t MAX_SCENARIOS = 16;

// Maximum number of I²C sensor modules
constexpr uint8_t MAX_MODULES = 8;

// Minimum plausible Unix timestamp (2001-09-09); used to detect valid NTP sync
constexpr time_t MIN_VALID_UNIX_TIME = 1000000000;

// Maximum timer duration in seconds (default: 24 hours)
constexpr uint32_t MAX_TIMER_DURATION_SEC = 86400;

// Maximum scenario action duration in seconds (default: 4 hours)
constexpr uint16_t MAX_SCENARIO_DURATION_SEC = 14400;

// callUrl action limits
constexpr uint8_t  CALL_URL_QUEUE_SIZE      = 4;
constexpr uint32_t CALL_URL_TIMEOUT_MS      = 8000;
constexpr uint16_t CALL_URL_MAX_URL_LEN     = 256;
constexpr uint16_t CALL_URL_MAX_HEADERS_LEN = 512;
constexpr uint16_t CALL_URL_MAX_BODY_LEN    = 512;

// =============================================================================
// Sensor Configuration (I2C)
// =============================================================================

// I2C addresses for sensor boards
constexpr uint8_t SENSOR_ADDR_TEMP1  = 0x40;
constexpr uint8_t SENSOR_ADDR_HUM1   = 0x00;  // Not connected yet
constexpr uint8_t SENSOR_ADDR_SOIL1  = 0x00;  // Not connected yet
constexpr uint8_t SENSOR_ADDR_WATER1 = 0x00;  // Not connected yet

// =============================================================================
// Timing Configuration
// =============================================================================

// How often to evaluate scenarios and timers (milliseconds)
constexpr unsigned long SCENARIO_CHECK_INTERVAL_MS = 5000;

// Watchdog timeout in milliseconds
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
  #define DEBUG_SCENARIO(fmt, ...) Serial.printf("[SCENARIO] " fmt, ##__VA_ARGS__)
  #define DD_DEBUG_WIFI(fmt, ...) Serial.printf("[WIFI] " fmt, ##__VA_ARGS__)
  #define DEBUG_API(fmt, ...) Serial.printf("[API] " fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
  #define DEBUG_VALVE(fmt, ...)
  #define DEBUG_SCENARIO(fmt, ...)
  #define DD_DEBUG_WIFI(fmt, ...)
  #define DEBUG_API(fmt, ...)
#endif

// =============================================================================
// Version Information
// =============================================================================

#define FIRMWARE_VERSION "4.0.7"
#define FIRMWARE_NAME "DripDrop"

// =============================================================================
// Storage
// =============================================================================

// LittleFS file for persisted settings
constexpr const char* SETTINGS_FILE = "/settings.json";

// LittleFS file for custom valve names
constexpr const char* VALVE_NAMES_FILE = "/valve_names.json";

// =============================================================================
// Include local overrides if available
// =============================================================================

#if __has_include("config_local.h")
  #include "config_local.h"
#endif

#endif // DRIPDROP_CONFIG_H
