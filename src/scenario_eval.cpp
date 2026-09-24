/**
 * DripDrop - Scenario condition evaluation
 *
 * ScenarioManager members that decide whether a scenario's conditions currently
 * hold. Split out of scenarios.cpp; compiled as its own translation unit for the
 * esp32dev build and #included by the native test harness.
 */

#include "scenarios.h"
#include "modules.h"

bool ScenarioManager::evaluateConditions(const JsonArray &conditions, const struct tm *timeInfo, time_t now,
                                         SensorCacheEntry *cache, uint8_t &cacheCount) const
{
  for (JsonObject cond : conditions)
  {
    const char *type = cond[SKey::TYPE];
    if (!type)
      return false; // Malformed condition — fail safe (never deref a null type)

    if (strcmp(type, SVal::TIME) == 0)
    {
      int hour = cond[SKey::HOUR];
      int minute = cond[SKey::MINUTE];
      if (timeInfo->tm_hour != hour || timeInfo->tm_min != minute)
      {
        return false;
      }
    }
    else if (strcmp(type, SVal::TIME_RANGE) == 0)
    {
      // Start-inclusive, end-exclusive window in minutes-of-day. When start >=
      // end the window wraps past midnight (e.g. 22:00-06:00). This is a gate:
      // the engine's edge detection fires the scenario once on window entry.
      int startMins = (int)cond[SKey::START_HOUR] * 60 + (int)cond[SKey::START_MINUTE];
      int endMins = (int)cond[SKey::END_HOUR] * 60 + (int)cond[SKey::END_MINUTE];
      int nowMins = timeInfo->tm_hour * 60 + timeInfo->tm_min;
      bool inRange = (startMins < endMins)
                         ? (nowMins >= startMins && nowMins < endMins)
                         : (nowMins >= startMins || nowMins < endMins);
      if (!inRange)
      {
        return false;
      }
    }
    else if (strcmp(type, SVal::DAY_OF_WEEK) == 0)
    {
      JsonArray days = cond[SKey::DAYS];
      int wday = timeInfo->tm_wday; // 0=Sun, 6=Sat
      if (!days[wday].as<bool>())
      {
        return false;
      }
    }
    else if (strcmp(type, SVal::SENSOR_VALUE) == 0)
    {
      const char *sensorId = cond[SKey::SENSOR_ID];
      const char *op = cond[SKey::OPERATOR];
      float threshold = cond[SKey::VALUE].as<float>();

      uint8_t addr = Modules.addrForUid(sensorId);
      if (addr == 0)
        return false;
      float reading;
      if (!readSensorCached(addr, reading, cache, cacheCount))
        return false;

      if (strcmp(op, SVal::OP_GT) == 0)
      {
        if (!(reading > threshold))
          return false;
      }
      else if (strcmp(op, SVal::OP_LT) == 0)
      {
        if (!(reading < threshold))
          return false;
      }
      else if (strcmp(op, SVal::OP_EQ) == 0)
      {
        if (reading != threshold)
          return false;
      }
    }
    else
    {
      return false; // Unknown condition type — fail safe
    }
  }

  return true;
}

bool ScenarioManager::readSensorCached(uint8_t addr, float &value,
                                       SensorCacheEntry *cache, uint8_t &cacheCount) const
{
  for (uint8_t i = 0; i < cacheCount; i++)
  {
    if (cache[i].addr == addr)
    {
      if (!cache[i].ok)
        return false;
      value = cache[i].value;
      return true;
    }
  }

  SensorResponse resp;
  bool ok = Modules.readModule(addr, resp);

  if (cacheCount < MAX_MODULES)
  {
    cache[cacheCount].addr = addr;
    cache[cacheCount].ok = ok;
    cache[cacheCount].value = ok ? resp.value : 0.0f;
    cacheCount++;
  }

  if (!ok)
    return false;
  value = resp.value;
  return true;
}
