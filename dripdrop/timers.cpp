/**
 * DripDrop - Timer Manager Implementation
 */

#include "timers.h"
#include "valves.h"

// Global instance
TimerManager Timers;

void TimerManager::begin() {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    _timers[i].valveId = i + 1;
    _timers[i].endTime = -1;  // Inactive
  }
  DEBUG_PRINTLN(F("Timers initialized"));
}

void TimerManager::check(time_t currentTime) {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (_timers[i].endTime < 0) continue;  // Inactive timer
    
    Valve* valve = Valves.getValve(i);
    if (!valve) continue;
    
    if (_timers[i].endTime > currentTime) {
      // Timer still active - ensure valve is on
      // Accept both TIMER and SCENARIO as valid sources (scenario uses timer for auto-shutoff)
      if (!valve->isOn) {
        Valves.setState(i, true, ValveSource::TIMER);
      }
    } else {
      // Timer expired - turn off valve
      DEBUG_PRINTF("Timer expired for valve %d\n", _timers[i].valveId);
      _timers[i].endTime = -1;
      
      // Only turn off if not manually controlled
      if (!valve->isManuallyControlled()) {
        Valves.setState(i, false, ValveSource::NONE);
      }
    }
  }
}

bool TimerManager::start(uint8_t valveId, uint32_t durationSeconds) {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) {
    DEBUG_PRINTF("Timer start failed: invalid valveId %d\n", valveId);
    return false;
  }
  
  if (durationSeconds == 0 || durationSeconds > MAX_TIMER_DURATION_SEC) {
    DEBUG_PRINTF("Timer start failed: invalid duration %lu\n", durationSeconds);
    return false;
  }
  
  time_t now = time(nullptr);
  _timers[index].endTime = now + durationSeconds;
  
  // Start the valve immediately
  Valves.setState(index, true, ValveSource::TIMER);
  
  DEBUG_PRINTF("Timer started for valve %d: %lu seconds (ends at %ld)\n", 
               valveId, durationSeconds, _timers[index].endTime);
  
  return true;
}

bool TimerManager::abort(uint8_t valveId) {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) {
    return false;
  }
  
  if (_timers[index].endTime < 0) {
    return false;  // No timer to abort
  }
  
  _timers[index].endTime = -1;

  Valve* valve = Valves.getValve(index);
  if (valve && !valve->isManuallyControlled()) {
    Valves.setState(index, false, ValveSource::NONE);
  }
  
  DEBUG_PRINTF("Timer aborted for valve %d\n", valveId);
  
  return true;
}

void TimerManager::abortAll() {
  for (uint8_t i = 0; i < NUM_VALVES; i++) {
    if (_timers[i].endTime >= 0) {
      abort(_timers[i].valveId);
    }
  }
}

Timer* TimerManager::get(uint8_t index) {
  if (index >= NUM_VALVES) return nullptr;
  return &_timers[index];
}

const Timer* TimerManager::get(uint8_t index) const {
  if (index >= NUM_VALVES) return nullptr;
  return &_timers[index];
}

bool TimerManager::isActive(uint8_t valveId, time_t currentTime) const {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) return false;
  return _timers[index].isActive(currentTime);
}

uint32_t TimerManager::getRemainingSeconds(uint8_t valveId, time_t currentTime) const {
  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) return 0;
  
  if (!_timers[index].isActive(currentTime)) {
    return 0;
  }
  
  return static_cast<uint32_t>(_timers[index].endTime - currentTime);
}
