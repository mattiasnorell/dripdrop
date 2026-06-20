/**
 * RelayController stub for native unit testing.
 */
#include "../../src/relays.h"

RelayController Relays;

static Relay stubRelays[NUM_RELAYS];

void RelayController::begin() {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    stubRelays[i].id = i + 1;
    stubRelays[i].pin = 0;
    stubRelays[i].lastRunStart = 0;
    stubRelays[i].lastRunEnd = 0;
    stubRelays[i].source = RelaySource::NONE;
    stubRelays[i].isOn = false;
    stubRelays[i].customName[0] = '\0';
  }
}

bool RelayController::setState(uint8_t index, bool on, RelaySource source) {
  if (index >= NUM_RELAYS) return false;
  stubRelays[index].isOn = on;
  stubRelays[index].source = on ? source : RelaySource::NONE;
  return true;
}

bool RelayController::getState(uint8_t index) const {
  if (index >= NUM_RELAYS) return false;
  return stubRelays[index].isOn;
}

Relay* RelayController::getRelay(uint8_t index) {
  if (index >= NUM_RELAYS) return nullptr;
  return &stubRelays[index];
}

const Relay* RelayController::getRelay(uint8_t index) const {
  if (index >= NUM_RELAYS) return nullptr;
  return &stubRelays[index];
}

int8_t RelayController::findByRelayId(uint8_t relayId) const {
  if (relayId >= 1 && relayId <= NUM_RELAYS) return relayId - 1;
  return -1;
}

void RelayController::allOff() {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    stubRelays[i].isOn = false;
    stubRelays[i].source = RelaySource::NONE;
  }
}

uint8_t RelayController::getActiveCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (stubRelays[i].isOn) count++;
  }
  return count;
}

bool RelayController::setCustomName(uint8_t index, const char* name) {
  if (index >= NUM_RELAYS) return false;
  if (name && name[0] != '\0') {
    strlcpy(stubRelays[index].customName, name, sizeof(stubRelays[index].customName));
  } else {
    stubRelays[index].customName[0] = '\0';
  }
  return true;
}

void RelayController::writeHardware(uint8_t, bool) {}
bool RelayController::readHardware(uint8_t) const { return false; }
void RelayController::loadNames() {}
void RelayController::saveNames() {}
