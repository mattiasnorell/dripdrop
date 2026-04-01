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
  SCHEDULE,      // Controlled by schedule
  TIMER,         // Controlled by one-time timer
  MANUAL         // Manually controlled via API
};

/**
 * Days of the week bitmask values
 */
namespace DayMask {
  constexpr uint8_t SUNDAY    = (1 << 7);  // 0b10000000
  constexpr uint8_t MONDAY    = (1 << 6);  // 0b01000000
  constexpr uint8_t TUESDAY   = (1 << 5);  // 0b00100000
  constexpr uint8_t WEDNESDAY = (1 << 4);  // 0b00010000
  constexpr uint8_t THURSDAY  = (1 << 3);  // 0b00001000
  constexpr uint8_t FRIDAY    = (1 << 2);  // 0b00000100
  constexpr uint8_t SATURDAY  = (1 << 1);  // 0b00000010
  constexpr uint8_t WEEKDAYS  = MONDAY | TUESDAY | WEDNESDAY | THURSDAY | FRIDAY;
  constexpr uint8_t WEEKEND   = SATURDAY | SUNDAY;
  constexpr uint8_t EVERY_DAY = WEEKDAYS | WEEKEND;
}

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
  inline bool isScheduleControlled() const { return source == ValveSource::SCHEDULE; }
  inline bool isTimerControlled() const { return source == ValveSource::TIMER; }
};

/**
 * Schedule entry for automated irrigation
 * Packed to minimize EEPROM usage
 */
struct __attribute__((packed)) Schedule {
  int8_t valveId;        // -1 indicates empty slot, 1-4 for valid valves
  uint8_t fromHour;      // 0-23
  uint8_t fromMinute;    // 0-59
  uint16_t duration;     // Duration in SECONDS (0-65535, ~18 hours max)
  uint8_t days;          // Bitmask using DayMask values
  
  // Helper methods
  inline bool isEmpty() const { return valveId < 0; }
  inline bool isValid() const { return valveId >= 1 && fromHour < 24 && fromMinute < 60; }
  inline uint32_t getDurationMinutes() const { return duration / 60; }
  inline uint32_t getDurationSeconds() const { return duration; }
  
  void clear() {
    valveId = -1;
    fromHour = 0;
    fromMinute = 0;
    duration = 0;
    days = 0;
  }
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
 * EEPROM data header for validation
 */
struct __attribute__((packed)) EepromHeader {
  uint32_t magic;        // Magic number to detect initialized EEPROM
  uint8_t version;       // Data format version for migrations
  uint8_t scheduleCount; // Number of schedules stored
  uint16_t checksum;     // Simple checksum for data integrity
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
  uint8_t activeSchedules;   // Number of configured schedules
  time_t currentTime;        // Current system time
};

// =============================================================================
// Constants
// =============================================================================

namespace EepromConfig {
  constexpr uint32_t MAGIC_NUMBER = 0x44524950;  // "DRIP" in ASCII
  constexpr uint8_t DATA_VERSION = 2;            // Increment when struct changes
  constexpr uint16_t HEADER_ADDR = 0;
  constexpr uint16_t HEADER_SIZE = sizeof(EepromHeader);
}

#endif // DRIPDROP_TYPES_H
