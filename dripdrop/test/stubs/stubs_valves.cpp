/**
 * ValveController stub for native unit testing.
 */
#include "../../valves.h"

ValveController Valves;

static Valve stubValves[NUM_VALVES];

void ValveController::begin() {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    stubValves[i].id = i + 1;
    stubValves[i].pin = 0;
    stubValves[i].lastRunStart = 0;
    stubValves[i].lastRunEnd = 0;
    stubValves[i].source = ValveSource::NONE;
    stubValves[i].isOn = false;
  }
}

bool ValveController::setState(uint8_t index, bool on, ValveSource source) {
  if (index >= NUM_VALVES) return false;
  stubValves[index].isOn = on;
  stubValves[index].source = on ? source : ValveSource::NONE;
  return true;
}

bool ValveController::getState(uint8_t index) const {
  if (index >= NUM_VALVES) return false;
  return stubValves[index].isOn;
}

Valve* ValveController::getValve(uint8_t index) {
  if (index >= NUM_VALVES) return nullptr;
  return &stubValves[index];
}

const Valve* ValveController::getValve(uint8_t index) const {
  if (index >= NUM_VALVES) return nullptr;
  return &stubValves[index];
}

int8_t ValveController::findByValveId(uint8_t valveId) const {
  if (valveId >= 1 && valveId <= NUM_VALVES) return valveId - 1;
  return -1;
}

void ValveController::allOff() {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    stubValves[i].isOn = false;
    stubValves[i].source = ValveSource::NONE;
  }
}

uint8_t ValveController::getActiveCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (stubValves[i].isOn) count++;
  }
  return count;
}

void ValveController::writeHardware(uint8_t, bool) {}
bool ValveController::readHardware(uint8_t) const { return false; }
