/**
 * DripDrop - Schedule Manager
 * 
 * Handles schedule storage, EEPROM persistence, and schedule checking.
 */

#ifndef DRIPDROP_SCHEDULER_H
#define DRIPDROP_SCHEDULER_H

#include <Arduino.h>
#include <EEPROM.h>
#include "types.h"
#include "config.h"

// =============================================================================
// Scheduler Class
// =============================================================================

class SchedulerClass {
public:
  /**
   * Initialize scheduler - call once in setup()
   */
  void begin();
  
  /**
   * Check schedules and update valve states - call periodically in loop()
   * @param currentTime Current Unix timestamp
   */
  void check(time_t currentTime);
  
  /**
   * Add a new schedule
   * @return Schedule index if successful, -1 if no slots available or invalid
   */
  int8_t add(uint8_t valveId, uint8_t fromHour, uint8_t fromMinute, 
             uint16_t duration, uint8_t days);
  
  /**
   * Update an existing schedule
   * @return true if successful
   */
  bool update(uint8_t scheduleId, uint8_t valveId, uint8_t fromHour, 
              uint8_t fromMinute, uint16_t duration, uint8_t days);
  
  /**
   * Delete a schedule
   * @return true if successful
   */
  bool remove(uint8_t scheduleId);
  
  /**
   * Delete all schedules
   */
  void removeAll();
  
  /**
   * Get schedule by index
   * @return Pointer to schedule, nullptr if invalid
   */
  Schedule* get(uint8_t index);
  const Schedule* get(uint8_t index) const;
  
  /**
   * Get count of active (non-empty) schedules
   */
  uint8_t getActiveCount() const;
  
  /**
   * Find first empty schedule slot
   * @return Slot index, or -1 if full
   */
  int8_t findEmptySlot() const;
  
  /**
   * Save schedules to EEPROM
   */
  void save();
  
  /**
   * Load schedules from EEPROM
   */
  void load();
  
  /**
   * Validate schedule parameters
   */
  static bool isValid(uint8_t fromHour, uint8_t fromMinute, uint16_t duration);
  
  /**
   * Check if a schedule should be active at the given time
   */
  bool isScheduleActive(const Schedule& schedule, const struct tm* timeInfo) const;
  
  /**
   * Get max schedules count
   */
  constexpr uint8_t maxSchedules() const { return MAX_SCHEDULES; }

private:
  Schedule _schedules[MAX_SCHEDULES];
  bool _dirty = false;  // Track if changes need saving
  
  /**
   * Calculate simple checksum for EEPROM validation
   */
  uint16_t calculateChecksum() const;
  
  /**
   * Initialize EEPROM with default values
   */
  void initializeEeprom();
};

// Global instance
extern SchedulerClass Scheduler;

#endif // DRIPDROP_SCHEDULER_H
