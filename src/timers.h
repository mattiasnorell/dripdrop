/**
 * DripDrop - Timer Manager
 * 
 * Handles one-time relay activation timers.
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
   * Check timers and update relay states - call periodically in loop()
   * @param currentTime Current Unix timestamp
   */
  void check(time_t currentTime);
  
  /**
   * Start a timer for a relay
   * @param relayId 1-based relay ID
   * @param durationSeconds Duration in seconds
   * @param source Logical owner the relay reports while the timer runs
   *               (TIMER for user timers, SCENARIO for scenario auto-off)
   * @return true if timer was set
   */
  bool start(uint8_t relayId, uint32_t durationSeconds, RelaySource source = RelaySource::TIMER);
  
  /**
   * Abort/cancel a timer
   * @param relayId 1-based relay ID
   * @return true if timer was cancelled
   */
  bool abort(uint8_t relayId);
  
  /**
   * Abort all active timers
   */
  void abortAll();
  
  /**
   * Get timer by relay index
   * @param index 0-based relay index
   * @return Pointer to timer, nullptr if invalid
   */
  Timer* get(uint8_t index);
  const Timer* get(uint8_t index) const;
  
  /**
   * Check if a relay has an active timer
   */
  bool isActive(uint8_t relayId, time_t currentTime) const;
  
  /**
   * Get remaining time for a relay's timer
   * @return Remaining seconds, or 0 if no active timer
   */
  uint32_t getRemainingSeconds(uint8_t relayId, time_t currentTime) const;

private:
  Timer _timers[NUM_RELAYS];
};

// Global instance
extern TimerManager Timers;

#endif // DRIPDROP_TIMERS_H
