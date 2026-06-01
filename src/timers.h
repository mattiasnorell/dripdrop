/**
 * DripDrop - Timer Manager
 * 
 * Handles one-time valve activation timers.
 */

#ifndef DRIPDROP_TIMERS_H
#define DRIPDROP_TIMERS_H

#include <Arduino.h>
#include "types.h"
#include "config.h"

// =============================================================================
// Timer Manager Class
// =============================================================================

class TimerManager {
public:
  /**
   * Initialize timers - call once in setup()
   */
  void begin();
  
  /**
   * Check timers and update valve states - call periodically in loop()
   * @param currentTime Current Unix timestamp
   */
  void check(time_t currentTime);
  
  /**
   * Start a timer for a valve
   * @param valveId 1-based valve ID
   * @param durationSeconds Duration in seconds
   * @return true if timer was set
   */
  bool start(uint8_t valveId, uint32_t durationSeconds);
  
  /**
   * Abort/cancel a timer
   * @param valveId 1-based valve ID
   * @return true if timer was cancelled
   */
  bool abort(uint8_t valveId);
  
  /**
   * Abort all active timers
   */
  void abortAll();
  
  /**
   * Get timer by valve index
   * @param index 0-based valve index
   * @return Pointer to timer, nullptr if invalid
   */
  Timer* get(uint8_t index);
  const Timer* get(uint8_t index) const;
  
  /**
   * Check if a valve has an active timer
   */
  bool isActive(uint8_t valveId, time_t currentTime) const;
  
  /**
   * Get remaining time for a valve's timer
   * @return Remaining seconds, or 0 if no active timer
   */
  uint32_t getRemainingSeconds(uint8_t valveId, time_t currentTime) const;

private:
  Timer _timers[NUM_VALVES];
};

// Global instance
extern TimerManager Timers;

#endif // DRIPDROP_TIMERS_H
