/**
 * TimerManager stub for native unit testing (used by scenario tests).
 */
#include "../../timers.h"
#include "../../valves.h"

TimerManager Timers;

void TimerManager::begin() {}
void TimerManager::check(time_t) {}

bool TimerManager::start(uint8_t valveId, uint32_t) {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) return false;
  Valves.setState(index, true, ValveSource::TIMER);
  return true;
}

bool TimerManager::abort(uint8_t valveId) {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) return false;
  Valves.setState(index, false, ValveSource::NONE);
  return true;
}

void TimerManager::abortAll() {}
Timer* TimerManager::get(uint8_t) { return nullptr; }
const Timer* TimerManager::get(uint8_t) const { return nullptr; }
bool TimerManager::isActive(uint8_t, time_t) const { return false; }
uint32_t TimerManager::getRemainingSeconds(uint8_t, time_t) const { return 0; }
