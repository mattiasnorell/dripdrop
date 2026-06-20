/**
 * DripDrop - Relay Controller
 * 
 * Manages relay state, hardware control, and provides an abstraction
 * layer for relay operations.
 */

#ifndef DRIPDROP_RELAYS_H
#define DRIPDROP_RELAYS_H

#include <Arduino.h>
#include "types.h"
#include "config.h"

// =============================================================================
// Relay Controller Class
// =============================================================================

class RelayController {
public:
  /**
   * Initialize all relays - call once in setup()
   */
  void begin();
  
  /**
   * Set relay state with source tracking
   * @param index Relay index (0-based)
   * @param on Target state
   * @param source What is controlling this relay
   * @return true if state changed, false if already in state or invalid index
   */
  bool setState(uint8_t index, bool on, RelaySource source = RelaySource::MANUAL);
  
  /**
   * Get relay state from hardware
   * @param index Relay index (0-based)
   * @return Current relay state
   */
  bool getState(uint8_t index) const;
  
  /**
   * Get relay info struct
   * @param index Relay index (0-based)
   * @return Pointer to relay struct, nullptr if invalid index
   */
  Relay* getRelay(uint8_t index);
  const Relay* getRelay(uint8_t index) const;
  
  /**
   * Find relay index by ID
   * @param relayId 1-based relay ID
   * @return 0-based index, or -1 if not found
   */
  int8_t findByRelayId(uint8_t relayId) const;
  
  /**
   * Turn off all relays
   */
  void allOff();
  
  /**
   * Get count of currently active relays
   */
  uint8_t getActiveCount() const;
  
  /**
   * Check if relay index is valid
   */
  inline bool isValidIndex(uint8_t index) const { return index < NUM_RELAYS; }
  
  /**
   * Check if relay ID is valid
   */
  inline bool isValidId(uint8_t id) const { return id >= 1 && id <= NUM_RELAYS; }
  
  /**
   * Get number of relays
   */
  constexpr uint8_t count() const { return NUM_RELAYS; }

  /**
   * Set a custom name for a relay and persist to flash
   * @param index Relay index (0-based)
   * @param name Custom name (max 31 chars), nullptr or "" to clear
   * @return true if successful, false if invalid index
   */
  bool setCustomName(uint8_t index, const char* name);

private:
  Relay _relays[NUM_RELAYS];
  
  /**
   * Write to relay hardware
   */
  void writeHardware(uint8_t index, bool on);

  /**
   * Read from relay hardware
   */
  bool readHardware(uint8_t index) const;

  /**
   * Load custom names from LittleFS
   */
  void loadNames();

  /**
   * Save custom names to LittleFS
   */
  void saveNames();
};

// Global instance
extern RelayController Relays;

#endif // DRIPDROP_RELAYS_H
