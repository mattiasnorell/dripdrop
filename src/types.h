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
 * Source of valve control - tracks WHY a valve is on
 */
enum class ValveSource : uint8_t {
  NONE = 0,      // Valve is off
  SCENARIO,      // Controlled by scenario automation
  TIMER,         // Controlled by one-time timer
  MANUAL         // Manually controlled via API
};

// =============================================================================
// Data Structures
// =============================================================================

/**
 * Valve runtime state
 */
struct Valve {
  uint8_t id;              // 1-based valve ID for API compatibility
  uint8_t pin;             // GPIO pin number
  time_t lastRunStart;     // Unix timestamp of last activation
  time_t lastRunEnd;       // Unix timestamp of last deactivation
  ValveSource source;      // Current control source
  bool isOn;               // Current state (cached for efficiency)

  // Helper methods
  inline bool isManuallyControlled() const { return source == ValveSource::MANUAL; }
  inline bool isScenarioControlled() const { return source == ValveSource::SCENARIO; }
  inline bool isTimerControlled() const { return source == ValveSource::TIMER; }
};

/**
 * One-time timer for temporary valve activation
 */
struct Timer {
  uint8_t valveId;       // Associated valve (1-based)
  time_t endTime;        // Unix timestamp when timer expires, -1 if inactive

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
  uint8_t activeValves;      // Number of currently active valves
  uint8_t activeScenarios;   // Number of configured scenarios
  time_t currentTime;        // Current system time
};

#endif // DRIPDROP_TYPES_H
