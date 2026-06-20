/**
 * DripDrop - Relay Controller Implementation
 */

#include "relays.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Global instance
RelayController Relays;

void RelayController::begin() {
  DEBUG_PRINTLN(F("Initializing relays..."));
  
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    _relays[i].id = i + 1;  // 1-based IDs for API compatibility
    _relays[i].pin = RELAY_PINS[i];
    _relays[i].lastRunStart = 0;
    _relays[i].lastRunEnd = 0;
    _relays[i].source = RelaySource::NONE;
    _relays[i].isOn = false;
    _relays[i].customName[0] = '\0';

    pinMode(_relays[i].pin, OUTPUT);
    writeHardware(i, false);  // Ensure all relays are off
    
    DEBUG_RELAY("Relay %d initialized on pin %d\n", _relays[i].id, _relays[i].pin);
  }

  loadNames();
}

bool RelayController::setState(uint8_t index, bool on, RelaySource source) {
  if (!isValidIndex(index)) {
    DEBUG_RELAY("Invalid relay index: %d\n", index);
    return false;
  }
  
  Relay& relay = _relays[index];
  
  // Check if state is actually changing (including source — e.g. SCENARIO→MANUAL matters)
  if (relay.isOn == on && relay.source == source) {
    return false;  // No change needed
  }
  
  // Update state
  relay.isOn = on;
  relay.source = on ? source : RelaySource::NONE;
  
  // Update timestamps
  time_t now = time(nullptr);
  if (on) {
    relay.lastRunStart = now;
  } else {
    relay.lastRunEnd = now;
  }
  
  // Write to hardware
  writeHardware(index, on);
  
  DEBUG_RELAY("Relay %d: %s (source: %d)\n", 
              relay.id, 
              on ? "ON" : "OFF", 
              static_cast<int>(relay.source));
  
  return true;
}

bool RelayController::getState(uint8_t index) const {
  if (!isValidIndex(index)) return false;
  return _relays[index].isOn;  // Use cache; hardware is only written by this firmware
}

Relay* RelayController::getRelay(uint8_t index) {
  if (!isValidIndex(index)) return nullptr;
  return &_relays[index];
}

const Relay* RelayController::getRelay(uint8_t index) const {
  if (!isValidIndex(index)) return nullptr;
  return &_relays[index];
}

int8_t RelayController::findByRelayId(uint8_t relayId) const {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (_relays[i].id == relayId) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

void RelayController::allOff() {
  DEBUG_RELAY("Turning all relays OFF\n");
  
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    setState(i, false, RelaySource::NONE);
  }
}

uint8_t RelayController::getActiveCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (_relays[i].isOn) count++;
  }
  return count;
}

void RelayController::writeHardware(uint8_t index, bool on) {
  if (!isValidIndex(index)) return;
  
  uint8_t level;
  if (RELAY_ACTIVE_HIGH) {
    level = on ? HIGH : LOW;
  } else {
    level = on ? LOW : HIGH;
  }
  
  digitalWrite(_relays[index].pin, level);
}

bool RelayController::readHardware(uint8_t index) const {
  if (!isValidIndex(index)) return false;

  uint8_t level = digitalRead(_relays[index].pin);

  if (RELAY_ACTIVE_HIGH) {
    return level == HIGH;
  } else {
    return level == LOW;
  }
}

bool RelayController::setCustomName(uint8_t index, const char* name) {
  if (!isValidIndex(index)) return false;

  if (name && name[0] != '\0') {
    strlcpy(_relays[index].customName, name, sizeof(_relays[index].customName));
  } else {
    _relays[index].customName[0] = '\0';
  }

  saveNames();
  return true;
}

void RelayController::loadNames() {
  File file = LittleFS.open(RELAY_NAMES_FILE, "r");
  if (!file) return;

  JsonDocument doc;
  if (deserializeJson(doc, file) != DeserializationError::Ok) {
    file.close();
    return;
  }
  file.close();

  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%d", _relays[i].id);
    if (doc[key].is<const char*>()) {
      strlcpy(_relays[i].customName, doc[key].as<const char*>(), sizeof(_relays[i].customName));
      DEBUG_RELAY("Relay %d name: %s\n", _relays[i].id, _relays[i].customName);
    }
  }
}

void RelayController::saveNames() {
  File file = LittleFS.open(RELAY_NAMES_FILE, "w");
  if (!file) return;

  JsonDocument doc;
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (_relays[i].customName[0] != '\0') {
      char key[4];
      snprintf(key, sizeof(key), "%d", _relays[i].id);
      doc[key] = _relays[i].customName;
    }
  }

  serializeJson(doc, file);
  file.close();
}
