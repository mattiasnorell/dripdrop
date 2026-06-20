/**
 * TimerManager stub for native unit testing (used by scenario tests).
 */
#include "../../src/timers.h"
#include "../../src/relays.h"

TimerManager Timers;

void TimerManager::begin() {}
void TimerManager::check(time_t) {}

bool TimerManager::start(uint8_t relayId, uint32_t) {
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0) return false;
  Relays.setState(index, true, RelaySource::TIMER);
  return true;
}

bool TimerManager::abort(uint8_t relayId) {
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0) return false;
  Relays.setState(index, false, RelaySource::NONE);
  return true;
}

void TimerManager::abortAll() {}
Timer* TimerManager::get(uint8_t) { return nullptr; }
const Timer* TimerManager::get(uint8_t) const { return nullptr; }
bool TimerManager::isActive(uint8_t, time_t) const { return false; }
uint32_t TimerManager::getRemainingSeconds(uint8_t, time_t) const { return 0; }
