/**
 * DripDrop - Valve Controller Implementation
 */

#include "valves.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Global instance
ValveController Valves;

void ValveController::begin() {
  DEBUG_PRINTLN(F("Initializing valves..."));
  
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    _valves[i].id = i + 1;  // 1-based IDs for API compatibility
    _valves[i].pin = VALVE_PINS[i];
    _valves[i].lastRunStart = 0;
    _valves[i].lastRunEnd = 0;
    _valves[i].source = ValveSource::NONE;
    _valves[i].isOn = false;
    _valves[i].customName[0] = '\0';

    pinMode(_valves[i].pin, OUTPUT);
    writeHardware(i, false);  // Ensure all valves are off
    
    DEBUG_VALVE("Valve %d initialized on pin %d\n", _valves[i].id, _valves[i].pin);
  }

  loadNames();
}

bool ValveController::setState(uint8_t index, bool on, ValveSource source) {
  if (!isValidIndex(index)) {
    DEBUG_VALVE("Invalid valve index: %d\n", index);
    return false;
  }
  
  Valve& valve = _valves[index];
  
  // Check if state is actually changing (including source — e.g. SCENARIO→MANUAL matters)
  if (valve.isOn == on && valve.source == source) {
    return false;  // No change needed
  }
  
  // Update state
  valve.isOn = on;
  valve.source = on ? source : ValveSource::NONE;
  
  // Update timestamps
  time_t now = time(nullptr);
  if (on) {
    valve.lastRunStart = now;
  } else {
    valve.lastRunEnd = now;
  }
  
  // Write to hardware
  writeHardware(index, on);
  
  DEBUG_VALVE("Valve %d: %s (source: %d)\n", 
              valve.id, 
              on ? "ON" : "OFF", 
              static_cast<int>(valve.source));
  
  return true;
}

bool ValveController::getState(uint8_t index) const {
  if (!isValidIndex(index)) return false;
  return _valves[index].isOn;  // Use cache; hardware is only written by this firmware
}

Valve* ValveController::getValve(uint8_t index) {
  if (!isValidIndex(index)) return nullptr;
  return &_valves[index];
}

const Valve* ValveController::getValve(uint8_t index) const {
  if (!isValidIndex(index)) return nullptr;
  return &_valves[index];
}

int8_t ValveController::findByValveId(uint8_t valveId) const {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (_valves[i].id == valveId) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

void ValveController::allOff() {
  DEBUG_VALVE("Turning all valves OFF\n");
  
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    setState(i, false, ValveSource::NONE);
  }
}

uint8_t ValveController::getActiveCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (_valves[i].isOn) count++;
  }
  return count;
}

void ValveController::writeHardware(uint8_t index, bool on) {
  if (!isValidIndex(index)) return;
  
  uint8_t level;
  if (VALVE_ACTIVE_HIGH) {
    level = on ? HIGH : LOW;
  } else {
    level = on ? LOW : HIGH;
  }
  
  digitalWrite(_valves[index].pin, level);
}

bool ValveController::readHardware(uint8_t index) const {
  if (!isValidIndex(index)) return false;

  uint8_t level = digitalRead(_valves[index].pin);

  if (VALVE_ACTIVE_HIGH) {
    return level == HIGH;
  } else {
    return level == LOW;
  }
}

bool ValveController::setCustomName(uint8_t index, const char* name) {
  if (!isValidIndex(index)) return false;

  if (name && name[0] != '\0') {
    strlcpy(_valves[index].customName, name, sizeof(_valves[index].customName));
  } else {
    _valves[index].customName[0] = '\0';
  }

  saveNames();
  return true;
}

void ValveController::loadNames() {
  File file = LittleFS.open(VALVE_NAMES_FILE, "r");
  if (!file) return;

  JsonDocument doc;
  if (deserializeJson(doc, file) != DeserializationError::Ok) {
    file.close();
    return;
  }
  file.close();

  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%d", _valves[i].id);
    if (doc[key].is<const char*>()) {
      strlcpy(_valves[i].customName, doc[key].as<const char*>(), sizeof(_valves[i].customName));
      DEBUG_VALVE("Valve %d name: %s\n", _valves[i].id, _valves[i].customName);
    }
  }
}

void ValveController::saveNames() {
  File file = LittleFS.open(VALVE_NAMES_FILE, "w");
  if (!file) return;

  JsonDocument doc;
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (_valves[i].customName[0] != '\0') {
      char key[4];
      snprintf(key, sizeof(key), "%d", _valves[i].id);
      doc[key] = _valves[i].customName;
    }
  }

  serializeJson(doc, file);
  file.close();
}
