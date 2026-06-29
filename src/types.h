/**
 * DripDrop - Type Definitions
 *
 * Core data structures and enumerations used throughout the application.
 */

#ifndef DRIPDROP_TYPES_H
#define DRIPDROP_TYPES_H

#include <Arduino.h>

// =============================================================================
// Enumerations
// =============================================================================

/**
 * Source of relay control - tracks WHY a relay is on
 */
enum class RelaySource : uint8_t {
  NONE = 0,      // Relay is off
  SCENARIO,      // Controlled by scenario automation
  TIMER,         // Controlled by one-time timer
  MANUAL         // Manually controlled via API
};

// =============================================================================
// Data Structures
// =============================================================================

/**
 * Relay runtime state
 */
struct Relay {
  uint8_t id;              // 1-based relay ID for API compatibility
  uint8_t pin;             // GPIO pin number
  time_t lastRunStart;     // Unix timestamp of last activation
  time_t lastRunEnd;       // Unix timestamp of last deactivation
  RelaySource source;      // Current control source
  bool isOn;               // Current state (cached for efficiency)
  char customName[32];     // User-defined name, empty string if not set

  // Helper methods
  inline bool isManuallyControlled() const { return source == RelaySource::MANUAL; }
  inline bool isScenarioControlled() const { return source == RelaySource::SCENARIO; }
  inline bool isTimerControlled() const { return source == RelaySource::TIMER; }
};

/**
 * One-time timer for temporary relay activation
 */
struct Timer {
  uint8_t relayId;       // Associated relay (1-based)
  time_t endTime;        // Unix timestamp when timer expires, -1 if inactive
  RelaySource source;    // Logical owner to restore the relay to while active
                         // (TIMER for user timers, SCENARIO for scenario auto-off)

  inline bool isActive(time_t now) const { return endTime > 0 && endTime > now; }
  inline void cancel() { endTime = -1; }
};

/**
 * System status information
 */
struct SystemStatus {
  uint32_t uptime;           // Milliseconds since boot
  uint32_t freeHeap;         // Free heap memory in bytes
  int8_t wifiRssi;           // WiFi signal strength in dBm
  bool wifiConnected;        // WiFi connection status
  bool ntpSynced;            // NTP time synchronization status
  bool apMode;               // Running in Access Point mode
  uint8_t activeRelays;      // Number of currently active relays
  uint8_t activeScenarios;   // Number of configured scenarios
  time_t currentTime;        // Current system time
};

#endif // DRIPDROP_TYPES_H
