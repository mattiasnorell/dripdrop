/**
 * DripDrop - Scenario Manager Implementation
 */

#include "scenarios.h"
#include "relays.h"
#include "timers.h"
#include "modules.h"
#include "mqtt.h"
#include "display.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

ScenarioManager Scenarios;

// =============================================================================
// Initialization
// =============================================================================

void ScenarioManager::begin()
{
  memset(_state, 0, sizeof(_state));
  load();

  DEBUG_SCENARIO("Loaded %d scenarios\n", _count);
}

// =============================================================================
// Storage
// =============================================================================

void ScenarioManager::load()
{
  File file = LittleFS.open(SCENARIOS_FILE, "r");
  if (!file)
  {
    DEBUG_SCENARIO("No scenarios file found, starting empty\n");
    _doc.to<JsonArray>();
    _count = 0;
    return;
  }

  DeserializationError err = deserializeJson(_doc, file);
  file.close();

  if (err)
  {
    DEBUG_SCENARIO("JSON parse error: %s, starting empty\n", err.c_str());
    _doc.to<JsonArray>();
    _count = 0;
    return;
  }

  // Fully re-validate loaded scenarios. Persisted files can be hand-edited,
  // imported via /config/import (which writes raw JSON), or written by a
  // different firmware version. Running validate() here guarantees check() never
  // dereferences a missing condition "type" / action "state" at runtime.
  JsonArray arr = _doc.as<JsonArray>();
  for (int i = arr.size() - 1; i >= 0; i--)
  {
    JsonObject s = arr[i];
    if (!s[SKey::ID].as<const char *>() || validate(s) != nullptr)
    {
      DEBUG_SCENARIO("Discarding invalid scenario at index %d\n", i);
      arr.remove(i);
    }
  }
  _count = arr.size();
}

void ScenarioManager::save()
{
  File file = LittleFS.open(SCENARIOS_FILE, "w");
  if (!file)
  {
    DEBUG_SCENARIO("Failed to open file for writing\n");
    return;
  }

  serializeJson(_doc, file);
  file.close();
  _dirty = false;
  _dirtyTime = 0;
  DEBUG_SCENARIO("Saved %d scenarios to LittleFS\n", _count);
}

void ScenarioManager::maybeSave(unsigned long now)
{
  if (_dirty && _dirtyTime > 0 && (now - _dirtyTime >= SCENARIO_SAVE_DEBOUNCE_MS))
  {
    save();
  }
}

// =============================================================================
// CRUD Operations
// =============================================================================

const char *ScenarioManager::validate(const JsonObject &input) const
{
  if (!input[SKey::NAME].is<const char *>())
  {
    return "Missing or invalid 'name'";
  }

  if (!input[SKey::CONDITIONS].is<JsonArray>())
  {
    return "Missing 'conditions' array";
  }

  JsonArray conditions = input[SKey::CONDITIONS];
  if (conditions.size() == 0)
  {
    return "At least one condition is required";
  }

  for (JsonObject cond : conditions)
  {
    const char *type = cond[SKey::TYPE];
    if (!type)
      return "Condition missing 'type'";

    if (strcmp(type, SVal::TIME) == 0)
    {
      if (!cond[SKey::HOUR].is<int>() || !cond[SKey::MINUTE].is<int>())
      {
        return "Time condition requires 'hour' and 'minute'";
      }
      int hour = cond[SKey::HOUR];
      int minute = cond[SKey::MINUTE];
      if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
      {
        return "Invalid time values";
      }
    }
    else if (strcmp(type, SVal::TIME_RANGE) == 0)
    {
      if (!cond[SKey::START_HOUR].is<int>() || !cond[SKey::START_MINUTE].is<int>() ||
          !cond[SKey::END_HOUR].is<int>() || !cond[SKey::END_MINUTE].is<int>())
      {
        return "timeRange requires startHour, startMinute, endHour, endMinute";
      }
      int startHour = cond[SKey::START_HOUR];
      int startMinute = cond[SKey::START_MINUTE];
      int endHour = cond[SKey::END_HOUR];
      int endMinute = cond[SKey::END_MINUTE];
      if (startHour < 0 || startHour > 23 || endHour < 0 || endHour > 23 ||
          startMinute < 0 || startMinute > 59 || endMinute < 0 || endMinute > 59)
      {
        return "Invalid timeRange values";
      }
      if (startHour * 60 + startMinute == endHour * 60 + endMinute)
      {
        return "timeRange start and end must differ";
      }
    }
    else if (strcmp(type, SVal::DAY_OF_WEEK) == 0)
    {
      if (!cond[SKey::DAYS].is<JsonArray>() || cond[SKey::DAYS].size() != 7)
      {
        return "dayOfWeek condition requires 'days' array of 7 booleans";
      }
    }
    else if (strcmp(type, SVal::SENSOR_VALUE) == 0)
    {
      if (!cond[SKey::SENSOR_ID].is<const char *>())
      {
        return "sensorValue condition requires 'sensorId'";
      }
      const char *op = cond[SKey::OPERATOR];
      if (!op || (strcmp(op, SVal::OP_GT) != 0 && strcmp(op, SVal::OP_LT) != 0 && strcmp(op, SVal::OP_EQ) != 0))
      {
        return "sensorValue condition requires 'operator' (gt, lt, eq)";
      }
      if (!cond[SKey::VALUE].is<float>())
      {
        return "sensorValue condition requires 'value'";
      }
    }
    else
    {
      return "Unknown condition type";
    }
  }

  if (!input[SKey::ACTIONS].is<JsonArray>())
  {
    return "Missing 'actions' array";
  }

  JsonArray actions = input[SKey::ACTIONS];
  if (actions.size() == 0)
  {
    return "At least one action is required";
  }

  for (JsonObject action : actions)
  {
    const char *type = action[SKey::TYPE] | SVal::RELAY; // default for backward compat

    if (strcmp(type, SVal::RELAY) == 0)
    {
      if (!action[SKey::RELAY_ID].is<int>())
      {
        return "Action missing 'relayId'";
      }
      int relayId = action[SKey::RELAY_ID];
      if (!Relays.isValidId(relayId))
      {
        return "Invalid relayId in action";
      }
      const char *state = action[SKey::STATE];
      if (!state || (strcmp(state, SVal::ON) != 0 && strcmp(state, SVal::OFF) != 0))
      {
        return "Action requires 'state' (on or off)";
      }
      if (strcmp(state, SVal::ON) == 0)
      {
        // Positive duration is required: a relay turned on by a scenario is
        // always driven through an auto-off timer, so 0 (no timer = latched on
        // forever) is rejected to avoid a relay that never closes.
        if (!action[SKey::DURATION].is<int>() || action[SKey::DURATION].as<int>() <= 0)
        {
          return "Action with state 'on' requires positive 'duration'";
        }
      }
    }
    else if (strcmp(type, SVal::DRIVER) == 0)
    {
      if (!action[SKey::UID].is<const char *>())
      {
        return "driver action requires 'uid'";
      }
      if (!action[SKey::CMD].is<int>())
      {
        return "driver action requires 'cmd'";
      }
    }
    else if (strcmp(type, SVal::CALL_URL) == 0)
    {
      const char *url = action[SKey::URL] | "";
      if (url[0] == '\0')
      {
        return "callUrl action requires 'url'";
      }
      if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
      {
        return "callUrl 'url' must start with http:// or https://";
      }
      const char *method = action[SKey::METHOD] | "";
      if (strcmp(method, SVal::GET) != 0 && strcmp(method, SVal::POST) != 0)
      {
        return "callUrl action requires 'method' (GET or POST)";
      }
    }
    else if (strcmp(type, SVal::DISPLAY_ACT) == 0)
    {
      if (!action[SKey::TIMEOUT].is<int>() || action[SKey::TIMEOUT].as<int>() < 0)
      {
        return "display action requires non-negative 'timeout'";
      }
    }
    // Unknown types pass validation — preserved for round-trip
  }

  // Optional: re-fire interval (seconds). Absent/0 = fire once on edge.
  if (!input[SKey::REPEAT_INTERVAL].isNull())
  {
    if (!input[SKey::REPEAT_INTERVAL].is<int>() || input[SKey::REPEAT_INTERVAL].as<int>() < 0)
    {
      return "repeatInterval must be a non-negative integer (seconds)";
    }
  }

  // Optional: enable flag. Absent = active (backward compat with pre-flag
  // scenarios). When present it must be a boolean.
  if (!input[SKey::IS_ACTIVE].isNull() && !input[SKey::IS_ACTIVE].is<bool>())
  {
    return "isActive must be a boolean";
  }

  return nullptr;
}

uint16_t ScenarioManager::nextId() const
{
  uint16_t maxId = 0;
  JsonArray arr = _doc.as<JsonArray>();
  for (JsonObject scenario : arr)
  {
    const char *idStr = scenario[SKey::ID].as<const char *>();
    if (!idStr)
      continue;
    uint16_t id = atoi(idStr);
    if (id > maxId)
      maxId = id;
  }
  return maxId + 1;
}

int ScenarioManager::findIndex(const char *id) const
{
  if (!id)
    return -1;
  JsonArray arr = _doc.as<JsonArray>();
  int i = 0;
  for (JsonObject scenario : arr)
  {
    const char *sid = scenario[SKey::ID].as<const char *>();
    if (sid && strcmp(sid, id) == 0)
    {
      return i;
    }
    i++;
  }
  return -1;
}

const char *ScenarioManager::add(const JsonObject &input, String &outId)
{
  if (_count >= MAX_SCENARIOS)
  {
    return "Maximum number of scenarios reached";
  }

  const char *err = validate(input);
  if (err)
    return err;

  uint16_t id = nextId();
  outId = String(id);

  JsonArray arr = _doc.as<JsonArray>();
  JsonObject scenario = arr.add<JsonObject>();
  scenario[SKey::ID] = outId;
  scenario[SKey::NAME] = input[SKey::NAME];
  scenario[SKey::CONDITIONS] = input[SKey::CONDITIONS];
  scenario[SKey::ACTIONS] = input[SKey::ACTIONS];
  if (input[SKey::REPEAT_INTERVAL].is<int>())
    scenario[SKey::REPEAT_INTERVAL] = input[SKey::REPEAT_INTERVAL].as<int>();
  // Default to active when the flag is absent (pre-flag scenarios stay running).
  scenario[SKey::IS_ACTIVE] = input[SKey::IS_ACTIVE] | true;

  _count++;
  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Added scenario '%s' (id=%s)\n",
                 input[SKey::NAME].as<const char *>(), outId.c_str());
  return nullptr;
}

const char *ScenarioManager::update(const char *id, const JsonObject &input)
{
  int idx = findIndex(id);
  if (idx < 0)
    return "Scenario not found";

  const char *err = validate(input);
  if (err)
    return err;

  JsonArray arr = _doc.as<JsonArray>();
  JsonObject scenario = arr[idx];

  scenario[SKey::NAME] = input[SKey::NAME];
  scenario[SKey::CONDITIONS] = input[SKey::CONDITIONS];
  scenario[SKey::ACTIONS] = input[SKey::ACTIONS];
  if (input[SKey::REPEAT_INTERVAL].is<int>())
    scenario[SKey::REPEAT_INTERVAL] = input[SKey::REPEAT_INTERVAL].as<int>();
  else
    scenario.remove(SKey::REPEAT_INTERVAL);
  // Default to active when the flag is absent (pre-flag scenarios stay running).
  scenario[SKey::IS_ACTIVE] = input[SKey::IS_ACTIVE] | true;

  // Reset runtime state since conditions may have changed
  clearState(atoi(id));

  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Updated scenario id=%s\n", id);
  return nullptr;
}

bool ScenarioManager::remove(const char *id)
{
  int idx = findIndex(id);
  if (idx < 0)
    return false;

  // Clear runtime state before removing
  const char *idStr = _doc.as<JsonArray>()[idx][SKey::ID].as<const char *>();
  if (idStr)
    clearState(atoi(idStr));

  JsonArray arr = _doc.as<JsonArray>();
  arr.remove(idx);
  _count--;

  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Removed scenario id=%s\n", id);
  return true;
}

void ScenarioManager::serialize(String &output) const
{
  // Build output array with lastRun injected from runtime state
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  JsonArray src = _doc.as<JsonArray>();
  for (JsonObject scenario : src)
  {
    JsonObject copy = arr.add<JsonObject>();
    for (JsonPair kv : scenario)
    {
      copy[kv.key()] = kv.value();
    }
    // Always surface isActive; legacy scenarios stored before the flag default
    // to active.
    copy[SKey::IS_ACTIVE] = scenario[SKey::IS_ACTIVE] | true;
    const char *idStr = scenario[SKey::ID].as<const char *>();
    if (idStr)
    {
      uint16_t id = atoi(idStr);
      // Find runtime state (const-safe linear scan)
      time_t lr = 0;
      for (uint8_t i = 0; i < MAX_SCENARIOS; i++)
      {
        if (_state[i].id == id)
        {
          lr = _state[i].lastRun;
          break;
        }
      }
      if (lr > 0)
      {
        copy[SKey::LAST_RUN] = (long)lr;
      }
      else
      {
        copy[SKey::LAST_RUN] = (char *)nullptr;
      }
    }
  }
  serializeJson(out, output);
}

ScenarioManager::RuntimeState *ScenarioManager::getState(uint16_t id)
{
  // Find existing slot
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++)
  {
    if (_state[i].id == id)
      return &_state[i];
  }
  // Find empty slot
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++)
  {
    if (_state[i].id == 0)
    {
      _state[i].id = id;
      _state[i].fired = false;
      _state[i].lastRun = 0;
      return &_state[i];
    }
  }
  return nullptr;
}

void ScenarioManager::clearState(uint16_t id)
{
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++)
  {
    if (_state[i].id == id)
    {
      _state[i] = {0, false, 0};
      return;
    }
  }
}

// =============================================================================
// Evaluation
// =============================================================================

void ScenarioManager::check(time_t currentTime)
{
  if (_count == 0)
    return;

  struct tm timeInfo;
  localtime_r(&currentTime, &timeInfo);

  // Sensor read cache shared across all scenarios for this cycle: each sensor
  // is read over I²C at most once per check() regardless of how many scenarios
  // reference it.
  SensorCacheEntry sensorCache[MAX_MODULES];
  uint8_t sensorCacheCount = 0;

  JsonArray arr = _doc.as<JsonArray>();

  for (JsonObject scenario : arr)
  {
    const char *idStr = scenario[SKey::ID].as<const char *>();
    if (!idStr)
      continue;

    uint16_t id = atoi(idStr);
    RuntimeState *rs = getState(id);
    if (!rs)
      continue;

    // Skip disabled scenarios. Absent flag = active (backward compat). Re-arm
    // fired state so it fires cleanly on the next rising edge once re-enabled.
    if (!(scenario[SKey::IS_ACTIVE] | true))
    {
      rs->fired = false;
      continue;
    }

    JsonArray conditions = scenario[SKey::CONDITIONS];
    bool allMatch = evaluateConditions(conditions, &timeInfo, currentTime,
                                       sensorCache, sensorCacheCount);

    // Re-fire support: with repeatInterval > 0 the scenario fires again every
    // repeatInterval seconds while conditions stay true (rounded up to the 5s
    // check cadence). Absent/0 keeps the fire-once-on-edge behavior.
    //
    // For a relay action this gives a duty cycle: the relay's auto-off timer
    // (its "duration") closes it after `duration` seconds, then the next re-fire
    // re-opens it. duration >= repeatInterval -> effectively always on (the
    // re-fire is a no-op while the timer is live); duration < repeatInterval ->
    // pulses on for `duration`, off for `repeatInterval - duration`.
    int repeatInterval = scenario[SKey::REPEAT_INTERVAL] | 0;
    bool shouldFire = allMatch &&
                      (!rs->fired ||
                       (repeatInterval > 0 && (currentTime - rs->lastRun) >= repeatInterval));

    if (shouldFire)
    {
      DEBUG_SCENARIO("Firing scenario '%s' (id=%s)\n",
                     scenario[SKey::NAME].as<const char *>(), idStr);

      JsonArray actions = scenario[SKey::ACTIONS];
      executeActions(actions, currentTime);

      char det[96];
      snprintf(det, sizeof(det), "{\"id\":\"%s\",\"name\":\"%s\"}",
               idStr, scenario[SKey::NAME].as<const char *>());
      Mqtt.publishEvent(LogLevel::INFO, LogEvent::SCENARIO_FIRE, det);

      rs->lastRun = currentTime;
      rs->fired = true;
    }
    else if (!allMatch && rs->fired) // re-arm for next rising edge
    {
      rs->fired = false;
    }
  }
}

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

void ScenarioManager::executeActions(const JsonArray &actions, time_t now)
{
  for (JsonObject action : actions)
  {
    const char *type = action[SKey::TYPE] | SVal::RELAY; // default for backward compat

    if (strcmp(type, SVal::RELAY) == 0)
    {
      uint8_t relayId = action[SKey::RELAY_ID];
      const char *state = action[SKey::STATE];
      if (!state)
        continue; // Malformed action — skip (never deref a null state)

      int8_t index = Relays.findByRelayId(relayId);
      if (index < 0)
        continue;

      const Relay *relay = Relays.getRelay(index);
      if (!relay)
        continue;

      // Respect priority: Manual > user Timer > Scenario.
      // A scenario-owned relay reports source SCENARIO even while its auto-off
      // timer runs (the timer remembers SCENARIO), so it stays re-assignable by
      // this or another scenario; only manual / user timers block us here.
      if (relay->isManuallyControlled() || relay->isTimerControlled())
      {
        DEBUG_SCENARIO("Skipping relay %d — overridden by %s\n",
                       relayId,
                       relay->isManuallyControlled() ? "manual" : "timer");
        continue;
      }

      if (strcmp(state, SVal::ON) == 0)
      {
        int duration = action[SKey::DURATION];
        if (duration > MAX_SCENARIO_DURATION_SEC)
        {
          duration = MAX_SCENARIO_DURATION_SEC;
        }

        // Idempotent re-fire: a repeatInterval scenario re-runs every cycle. If
        // this scenario already holds the relay on with a live auto-off timer,
        // skip it — no GPIO write, no MQTT, no timer reset. After the timer
        // expires the relay is off, so a re-fire here re-arms it (the intended
        // duty-cycle when duration < repeatInterval).
        if (relay->isOn && relay->isScenarioControlled() &&
            Timers.isActive(relayId, now))
        {
          continue;
        }

        // The auto-off timer drives the relay but keeps SCENARIO as the logical
        // owner (see Timers.start source param), so we no longer double-write
        // the pin or mask the timer behind a TIMER source.
        Timers.start(relayId, duration, RelaySource::SCENARIO);
        Mqtt.publishRelayState(relayId);
        DEBUG_SCENARIO("Relay %d ON for %d sec\n", relayId, duration);
      }
      else
      {
        // state == "off". Timers.abort() already drops the relay if a timer was
        // running; the setState covers the no-timer case and is an idempotent
        // no-op otherwise. Publish unconditionally so the off state is always
        // reported (abort() does not publish relay state itself).
        Timers.abort(relayId);
        Relays.setState(index, false, RelaySource::NONE);
        Mqtt.publishRelayState(relayId);
        DEBUG_SCENARIO("Relay %d OFF\n", relayId);
      }
    }
    else if (strcmp(type, SVal::CALL_URL) == 0)
    {
      if (_callUrlCount >= CALL_URL_QUEUE_SIZE)
      {
        DEBUG_SCENARIO("callUrl queue full, dropping request\n");
        continue;
      }
      CallUrlRequest &req = _callUrlQueue[_callUrlCount++];
      const char *url = action[SKey::URL] | "";
      const char *method = action[SKey::METHOD] | SVal::GET;
      const char *headers = action[SKey::HEADERS] | "";
      const char *body = action[SKey::BODY] | "";
      req.url.reserve(CALL_URL_MAX_URL_LEN);
      req.url = url;
      req.url = req.url.substring(0, CALL_URL_MAX_URL_LEN);
      req.method = method;
      req.headers = String(headers).substring(0, CALL_URL_MAX_HEADERS_LEN);
      req.body = String(body).substring(0, CALL_URL_MAX_BODY_LEN);
    }
    else if (strcmp(type, SVal::DRIVER) == 0)
    {
      const char *uid = action[SKey::UID] | "";
      uint8_t cmd = action[SKey::CMD].as<int>();
      uint8_t addr = Modules.addrForUid(uid);
      if (addr == 0)
      {
        DEBUG_SCENARIO("driver action: uid=%s not registered, skipping\n", uid);
        continue;
      }
      if (!Modules.commandModule(addr, cmd))
      {
        DEBUG_SCENARIO("driver action: commandModule failed for uid=%s cmd=%d\n", uid, cmd);
      }
    }
    else if (strcmp(type, SVal::DISPLAY_ACT) == 0)
    {
      // Note: with repeatInterval set this re-applies every cycle. Give the
      // override a timeout >= repeatInterval so it refreshes seamlessly instead
      // of lapsing to the normal screen and snapping back (visible flicker).
      int timeout = action[SKey::TIMEOUT] | 0;
      if (timeout == 0)
      {
        DEBUG_SCENARIO("display action: timeout=0, skipping\n");
        continue;
      }
      String r0 = action[SKey::ROW0] | "";
      String r1 = action[SKey::ROW1] | "";
      String r2 = action[SKey::ROW2] | "";
      String r3 = action[SKey::ROW3] | "";
      Display.showOverride(r0, r1, r2, r3, (uint32_t)timeout);
      DEBUG_SCENARIO("display action: override for %d sec\n", timeout);
    }
    else
    {
      DEBUG_SCENARIO("Unknown action type '%s', skipping\n", type);
    }
  }
}

// =============================================================================
// callUrl Execution
// =============================================================================

static void applyParsedHeaders(HTTPClient &http, const String &raw)
{
  int start = 0;
  int len = raw.length();
  while (start < len)
  {
    int nl = raw.indexOf('\n', start);
    String line = (nl < 0) ? raw.substring(start) : raw.substring(start, nl);
    start = (nl < 0) ? len : nl + 1;

    line.trim();
    if (line.length() == 0)
      continue;

    int colon = line.indexOf(':');
    if (colon <= 0)
      continue; // no colon or colon at position 0 — malformed

    String key = line.substring(0, colon);
    String val = line.substring(colon + 1);
    key.trim();
    val.trim();
    if (key.length() > 0)
    {
      http.addHeader(key, val);
    }
  }
}

// Issue the configured request on an already-begun HTTPClient and return the code.
static int performCallUrl(HTTPClient &http, const CallUrlRequest &req)
{
  http.setTimeout(CALL_URL_TIMEOUT_MS);
  applyParsedHeaders(http, req.headers);

  if (req.method == SVal::POST)
  {
    http.addHeader("Content-Length", String(req.body.length()));
    return http.POST(req.body);
  }
  return http.GET();
}

static void executeCallUrl(const CallUrlRequest &req)
{
  unsigned long t0 = millis();
  HTTPClient http;
  int code;

  if (req.url.startsWith("https://"))
  {
    // HTTPS: cert verification skipped — standard for embedded devices
    WiFiClientSecure *secureClient = new WiFiClientSecure;
    secureClient->setInsecure();
    http.begin(*secureClient, req.url);
    code = performCallUrl(http, req);
    http.end();
    delete secureClient;
  }
  else
  {
    http.begin(req.url);
    code = performCallUrl(http, req);
    http.end();
  }

  DEBUG_SCENARIO("callUrl %s → %d (%lums)\n", req.url.c_str(), code, millis() - t0);
}

bool ScenarioManager::run(const char* id, time_t now)
{
  int idx = findIndex(id);
  if (idx < 0) return false;

  JsonArray arr = _doc.as<JsonArray>();
  JsonObject scenario = arr[idx];

  JsonArray actions = scenario[SKey::ACTIONS];
  executeActions(actions, now);

  // A manual run counts as a fire: record lastRun and set fired so check()
  // treats it as the current edge. This intentionally (a) suppresses an
  // immediate duplicate auto-fire if conditions already hold, and (b) starts the
  // repeatInterval clock from this run — the next automatic re-fire is one
  // interval later. A later falling edge in check() re-arms it as usual.
  uint16_t scenarioId = atoi(id);
  RuntimeState* rs = getState(scenarioId);
  if (rs)
  {
    rs->lastRun = now;
    rs->fired = true;
  }

  char det[96];
  snprintf(det, sizeof(det), "{\"id\":\"%s\",\"name\":\"%s\"}",
           id, scenario[SKey::NAME] | "");
  Mqtt.publishEvent(LogLevel::INFO, LogEvent::SCENARIO_FIRE, det);

  DEBUG_SCENARIO("Manual run of scenario id=%s\n", id);
  return true;
}

void ScenarioManager::drainCallUrlQueue()
{
  for (uint8_t i = 0; i < _callUrlCount; i++)
  {
#if defined(ESP32)
    // Each request blocks up to CALL_URL_TIMEOUT_MS; pet the watchdog between
    // them so a full queue of slow hosts can't exceed WATCHDOG_TIMEOUT_MS before
    // loop() resets it.
    esp_task_wdt_reset();
#endif
    executeCallUrl(_callUrlQueue[i]);
  }
  _callUrlCount = 0;
}
