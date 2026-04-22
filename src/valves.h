/**
 * DripDrop - Valve Controller
 * 
 * Manages valve state, hardware control, and provides an abstraction
 * layer for valve operations.
 */

#ifndef DRIPDROP_VALVES_H
#define DRIPDROP_VALVES_H

#include <Arduino.h>
#include "types.h"
#include "config.h"

// =============================================================================
// Valve Controller Class
// =============================================================================

class ValveController {
public:
  /**
   * Initialize all valves - call once in setup()
   */
  void begin();
  
  /**
   * Set valve state with source tracking
   * @param index Valve index (0-based)
   * @param on Target state
   * @param source What is controlling this valve
   * @return true if state changed, false if already in state or invalid index
   */
  bool setState(uint8_t index, bool on, ValveSource source = ValveSource::MANUAL);
  
  /**
   * Get valve state from hardware
   * @param index Valve index (0-based)
   * @return Current valve state
   */
  bool getState(uint8_t index) const;
  
  /**
   * Get valve info struct
   * @param index Valve index (0-based)
   * @return Pointer to valve struct, nullptr if invalid index
   */
  Valve* getValve(uint8_t index);
  const Valve* getValve(uint8_t index) const;
  
  /**
   * Find valve index by ID
   * @param valveId 1-based valve ID
   * @return 0-based index, or -1 if not found
   */
  int8_t findByValveId(uint8_t valveId) const;
  
  /**
   * Turn off all valves
   */
  void allOff();
  
  /**
   * Get count of currently active valves
   */
  uint8_t getActiveCount() const;
  
  /**
   * Check if valve index is valid
   */
  inline bool isValidIndex(uint8_t index) const { return index < NUM_VALVES; }
  
  /**
   * Check if valve ID is valid
   */
  inline bool isValidId(uint8_t id) const { return id >= 1 && id <= NUM_VALVES; }
  
  /**
   * Get number of valves
   */
  constexpr uint8_t count() const { return NUM_VALVES; }

private:
  Valve _valves[NUM_VALVES];
  
  /**
   * Write to valve hardware
   */
  void writeHardware(uint8_t index, bool on);
  
  /**
   * Read from valve hardware
   */
  bool readHardware(uint8_t index) const;
};

// Global instance
extern ValveController Valves;

#endif // DRIPDROP_VALVES_H
