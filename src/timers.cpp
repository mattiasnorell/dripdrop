/**
 * DripDrop - Timer Manager Implementation
 */

#include "timers.h"
#include "relays.h"
#include "mqtt.h"

// Global instance
TimerManager Timers;

void TimerManager::begin()
{
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    _timers[i].relayId = i + 1;
    _timers[i].endTime = -1; // Inactive
    _timers[i].source = RelaySource::NONE;
  }
  DEBUG_PRINTLN(F("Timers initialized"));
}

void TimerManager::check(time_t currentTime)
{
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    if (_timers[i].endTime < 0)
      continue; // Inactive timer

    Relay *relay = Relays.getRelay(i);
    if (!relay)
      continue;

    if (_timers[i].endTime > currentTime)
    {
      // Timer still active - ensure relay is on, restoring the logical owner
      // that started the timer (TIMER for user timers, SCENARIO for auto-shutoff)
      if (!relay->isOn)
      {
        Relays.setState(i, true, _timers[i].source);
      }
    }
    else
    {
      // Timer expired - turn off relay
      uint8_t relayId = _timers[i].relayId;
      DEBUG_PRINTF("Timer expired for relay %d\n", relayId);
      _timers[i].endTime = -1;

      char det[32];
      snprintf(det, sizeof(det), "{\"relayId\":%d}", relayId);
      Mqtt.publishEvent(LogLevel::INFO, LogEvent::TIMER_EXPIRE, det);

      // Only turn off if not manually controlled
      if (!relay->isManuallyControlled())
      {
        Relays.setState(i, false, RelaySource::NONE);
      }
      Mqtt.publishRelayState(relayId);
    }
  }
}

bool TimerManager::start(uint8_t relayId, uint32_t durationSeconds, RelaySource source)
{
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0)
  {
    DEBUG_PRINTF("Timer start failed: invalid relayId %d\n", relayId);
    return false;
  }

  if (durationSeconds == 0 || durationSeconds > MAX_TIMER_DURATION_SEC)
  {
    DEBUG_PRINTF("Timer start failed: invalid duration %lu\n", durationSeconds);
    return false;
  }

  time_t now = time(nullptr);
  _timers[index].endTime = now + durationSeconds;
  _timers[index].source = source;

  // Start the relay immediately under the requested logical owner
  Relays.setState(index, true, source);

  DEBUG_PRINTF("Timer started for relay %d: %lu seconds (ends at %ld)\n",
               relayId, durationSeconds, _timers[index].endTime);

  char det[64];
  snprintf(det, sizeof(det), "{\"relayId\":%d,\"duration\":%lu}", relayId, (unsigned long)durationSeconds);
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::TIMER_START, det);

  return true;
}

bool TimerManager::abort(uint8_t relayId)
{
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0)
  {
    return false;
  }

  if (_timers[index].endTime < 0)
  {
    return false; // No timer to abort
  }

  _timers[index].endTime = -1;

  Relay *relay = Relays.getRelay(index);
  if (relay && !relay->isManuallyControlled())
  {
    Relays.setState(index, false, RelaySource::NONE);
  }

  DEBUG_PRINTF("Timer aborted for relay %d\n", relayId);

  char det[32];
  snprintf(det, sizeof(det), "{\"relayId\":%d}", relayId);
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::TIMER_ABORT, det);

  return true;
}

void TimerManager::abortAll()
{
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
  {
    if (_timers[i].endTime >= 0)
    {
      abort(_timers[i].relayId);
    }
  }
}

Timer *TimerManager::get(uint8_t index)
{
  if (index >= NUM_RELAYS)
    return nullptr;
  return &_timers[index];
}

const Timer *TimerManager::get(uint8_t index) const
{
  if (index >= NUM_RELAYS)
    return nullptr;
  return &_timers[index];
}

bool TimerManager::isActive(uint8_t relayId, time_t currentTime) const
{
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0)
    return false;
  return _timers[index].isActive(currentTime);
}

uint32_t TimerManager::getRemainingSeconds(uint8_t relayId, time_t currentTime) const
{
  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0)
    return 0;

  if (!_timers[index].isActive(currentTime))
  {
    return 0;
  }

  return static_cast<uint32_t>(_timers[index].endTime - currentTime);
}
