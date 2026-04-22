/**
 * DripDrop - Scenario Manager Implementation
 */

#include "scenarios.h"
#include "valves.h"
#include "timers.h"
#include "sensors.h"
#include "logger.h"
#include <LittleFS.h>

ScenarioManager Scenarios;

// =============================================================================
// Initialization
// =============================================================================

void ScenarioManager::begin() {
  memset(_state, 0, sizeof(_state));
  load();

  DEBUG_SCENARIO("Loaded %d scenarios\n", _count);
}

// =============================================================================
// Storage
// =============================================================================

void ScenarioManager::load() {
  File file = LittleFS.open(SCENARIOS_FILE, "r");
  if (!file) {
    DEBUG_SCENARIO("No scenarios file found, starting empty\n");
    _doc.to<JsonArray>();
    _count = 0;
    return;
  }

  DeserializationError err = deserializeJson(_doc, file);
  file.close();

  if (err) {
    DEBUG_SCENARIO("JSON parse error: %s, starting empty\n", err.c_str());
    _doc.to<JsonArray>();
    _count = 0;
    return;
  }

  // Validate loaded scenarios — remove entries with missing required fields
  JsonArray arr = _doc.as<JsonArray>();
  for (int i = arr.size() - 1; i >= 0; i--) {
    JsonObject s = arr[i];
    const char* id = s["id"].as<const char*>();
    if (!id || !s["name"].is<const char*>() ||
        !s["conditions"].is<JsonArray>() || !s["actions"].is<JsonArray>()) {
      DEBUG_SCENARIO("Discarding invalid scenario at index %d\n", i);
      arr.remove(i);
    }
  }
  _count = arr.size();
}

void ScenarioManager::save() {
  File file = LittleFS.open(SCENARIOS_FILE, "w");
  if (!file) {
    DEBUG_SCENARIO("Failed to open file for writing\n");
    return;
  }

  serializeJson(_doc, file);
  file.close();
  _dirty = false;
  _dirtyTime = 0;
  DEBUG_SCENARIO("Saved %d scenarios to LittleFS\n", _count);
}

void ScenarioManager::maybeSave(unsigned long now) {
  if (_dirty && _dirtyTime > 0 && (now - _dirtyTime >= SCENARIO_SAVE_DEBOUNCE_MS)) {
    save();
  }
}

// =============================================================================
// CRUD Operations
// =============================================================================

const char* ScenarioManager::validate(const JsonObject& input) const {
  if (!input["name"].is<const char*>()) {
    return "Missing or invalid 'name'";
  }

  if (!input["conditions"].is<JsonArray>()) {
    return "Missing 'conditions' array";
  }

  JsonArray conditions = input["conditions"];
  if (conditions.size() == 0) {
    return "At least one condition is required";
  }

  for (JsonObject cond : conditions) {
    const char* type = cond["type"];
    if (!type) return "Condition missing 'type'";

    if (strcmp(type, "time") == 0) {
      if (!cond["hour"].is<int>() || !cond["minute"].is<int>()) {
        return "Time condition requires 'hour' and 'minute'";
      }
      int hour = cond["hour"];
      int minute = cond["minute"];
      if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return "Invalid time values";
      }
    } else if (strcmp(type, "dayOfWeek") == 0) {
      if (!cond["days"].is<JsonArray>() || cond["days"].size() != 7) {
        return "dayOfWeek condition requires 'days' array of 7 booleans";
      }
    } else if (strcmp(type, "sensorValue") == 0) {
      if (!cond["sensorId"].is<const char*>()) {
        return "sensorValue condition requires 'sensorId'";
      }
      const char* op = cond["operator"];
      if (!op || (strcmp(op, "gt") != 0 && strcmp(op, "lt") != 0 && strcmp(op, "eq") != 0)) {
        return "sensorValue condition requires 'operator' (gt, lt, eq)";
      }
      if (!cond["value"].is<int>()) {
        return "sensorValue condition requires 'value'";
      }
    } else {
      return "Unknown condition type";
    }
  }

  if (!input["actions"].is<JsonArray>()) {
    return "Missing 'actions' array";
  }

  JsonArray actions = input["actions"];
  if (actions.size() == 0) {
    return "At least one action is required";
  }

  for (JsonObject action : actions) {
    if (!action["valveId"].is<int>()) {
      return "Action missing 'valveId'";
    }
    int valveId = action["valveId"];
    if (!Valves.isValidId(valveId)) {
      return "Invalid valveId in action";
    }
    const char* state = action["state"];
    if (!state || (strcmp(state, "on") != 0 && strcmp(state, "off") != 0)) {
      return "Action requires 'state' (on or off)";
    }
    if (strcmp(state, "on") == 0) {
      if (!action["duration"].is<int>() || action["duration"].as<int>() <= 0) {
        return "Action with state 'on' requires positive 'duration'";
      }
    }
  }

  return nullptr;
}

uint16_t ScenarioManager::nextId() const {
  uint16_t maxId = 0;
  JsonArray arr = _doc.as<JsonArray>();
  for (JsonObject scenario : arr) {
    const char* idStr = scenario["id"].as<const char*>();
    if (!idStr) continue;
    uint16_t id = atoi(idStr);
    if (id > maxId) maxId = id;
  }
  return maxId + 1;
}

int ScenarioManager::findIndex(const char* id) const {
  if (!id) return -1;
  JsonArray arr = _doc.as<JsonArray>();
  int i = 0;
  for (JsonObject scenario : arr) {
    const char* sid = scenario["id"].as<const char*>();
    if (sid && strcmp(sid, id) == 0) {
      return i;
    }
    i++;
  }
  return -1;
}

const char* ScenarioManager::add(const JsonObject& input, String& outId) {
  if (_count >= MAX_SCENARIOS) {
    return "Maximum number of scenarios reached";
  }

  const char* err = validate(input);
  if (err) return err;

  uint16_t id = nextId();
  outId = String(id);

  JsonArray arr = _doc.as<JsonArray>();
  JsonObject scenario = arr.add<JsonObject>();
  scenario["id"] = outId;
  scenario["name"] = input["name"];
  scenario["conditions"] = input["conditions"];
  scenario["actions"] = input["actions"];

  _count++;
  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Added scenario '%s' (id=%s)\n",
                 input["name"].as<const char*>(), outId.c_str());
  return nullptr;
}

const char* ScenarioManager::update(const char* id, const JsonObject& input) {
  int idx = findIndex(id);
  if (idx < 0) return "Scenario not found";

  const char* err = validate(input);
  if (err) return err;

  JsonArray arr = _doc.as<JsonArray>();
  JsonObject scenario = arr[idx];

  scenario["name"] = input["name"];
  scenario["conditions"] = input["conditions"];
  scenario["actions"] = input["actions"];

  // Reset runtime state since conditions may have changed
  clearState(atoi(id));

  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Updated scenario id=%s\n", id);
  return nullptr;
}

bool ScenarioManager::remove(const char* id) {
  int idx = findIndex(id);
  if (idx < 0) return false;

  // Clear runtime state before removing
  const char* idStr = _doc.as<JsonArray>()[idx]["id"].as<const char*>();
  if (idStr) clearState(atoi(idStr));

  JsonArray arr = _doc.as<JsonArray>();
  arr.remove(idx);
  _count--;

  _dirty = true;
  _dirtyTime = millis();

  DEBUG_SCENARIO("Removed scenario id=%s\n", id);
  return true;
}

void ScenarioManager::serialize(String& output) const {
  // Build output array with lastRun injected from runtime state
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  JsonArray src = _doc.as<JsonArray>();
  for (JsonObject scenario : src) {
    JsonObject copy = arr.add<JsonObject>();
    for (JsonPair kv : scenario) {
      copy[kv.key()] = kv.value();
    }
    const char* idStr = scenario["id"].as<const char*>();
    if (idStr) {
      uint16_t id = atoi(idStr);
      // Find runtime state (const-safe linear scan)
      time_t lr = 0;
      for (uint8_t i = 0; i < MAX_SCENARIOS; i++) {
        if (_state[i].id == id) { lr = _state[i].lastRun; break; }
      }
      if (lr > 0) {
        copy["lastRun"] = (long)lr;
      } else {
        copy["lastRun"] = (char*)nullptr;
      }
    }
  }
  serializeJson(out, output);
}

ScenarioManager::RuntimeState* ScenarioManager::getState(uint16_t id) {
  // Find existing slot
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++) {
    if (_state[i].id == id) return &_state[i];
  }
  // Find empty slot
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++) {
    if (_state[i].id == 0) {
      _state[i].id = id;
      _state[i].fired = false;
      _state[i].lastRun = 0;
      return &_state[i];
    }
  }
  return nullptr;
}

void ScenarioManager::clearState(uint16_t id) {
  for (uint8_t i = 0; i < MAX_SCENARIOS; i++) {
    if (_state[i].id == id) {
      _state[i] = {0, false, 0};
      return;
    }
  }
}

// =============================================================================
// Evaluation
// =============================================================================

void ScenarioManager::check(time_t currentTime) {
  if (_count == 0) return;

  struct tm timeInfo;
  localtime_r(&currentTime, &timeInfo);

  JsonArray arr = _doc.as<JsonArray>();

  for (JsonObject scenario : arr) {
    const char* idStr = scenario["id"].as<const char*>();
    if (!idStr) continue;

    uint16_t id = atoi(idStr);
    RuntimeState* rs = getState(id);
    if (!rs) continue;

    JsonArray conditions = scenario["conditions"];
    bool allMatch = evaluateConditions(conditions, &timeInfo, currentTime);

    if (allMatch && !rs->fired) {
      DEBUG_SCENARIO("Firing scenario '%s' (id=%s)\n",
                     scenario["name"].as<const char*>(), idStr);

      JsonArray actions = scenario["actions"];
      executeActions(actions, currentTime);

      char det[96];
      snprintf(det, sizeof(det), "{\"id\":\"%s\",\"name\":\"%s\"}",
               idStr, scenario["name"].as<const char*>());
      Logger.Info(LogEvent::SCENARIO_FIRE, det);

      rs->lastRun = currentTime;
      rs->fired = true;
    } else if (!allMatch && rs->fired) {
      rs->fired = false;
    }
  }
}

bool ScenarioManager::evaluateConditions(const JsonArray& conditions, const struct tm* timeInfo, time_t now) const {
  for (JsonObject cond : conditions) {
    const char* type = cond["type"];

    if (strcmp(type, "time") == 0) {
      int hour = cond["hour"];
      int minute = cond["minute"];
      if (timeInfo->tm_hour != hour || timeInfo->tm_min != minute) {
        return false;
      }
    } else if (strcmp(type, "dayOfWeek") == 0) {
      JsonArray days = cond["days"];
      int wday = timeInfo->tm_wday;  // 0=Sun, 6=Sat
      if (!days[wday].as<bool>()) {
        return false;
      }
    } else if (strcmp(type, "sensorValue") == 0) {
      const char* sensorId = cond["sensorId"];
      const char* op = cond["operator"];
      int threshold = cond["value"];

      int16_t reading = Sensors.read(sensorId);
      if (reading == SENSOR_READ_ERROR) {
        return false;  // Sensor unavailable — don't fire
      }

      if (strcmp(op, "gt") == 0) {
        if (!(reading > threshold)) return false;
      } else if (strcmp(op, "lt") == 0) {
        if (!(reading < threshold)) return false;
      } else if (strcmp(op, "eq") == 0) {
        if (reading != threshold) return false;
      }
    } else {
      return false;  // Unknown condition type — fail safe
    }
  }

  return true;
}

void ScenarioManager::executeActions(const JsonArray& actions, time_t now) {
  for (JsonObject action : actions) {
    uint8_t valveId = action["valveId"];
    const char* state = action["state"];

    int8_t index = Valves.findByValveId(valveId);
    if (index < 0) continue;

    const Valve* valve = Valves.getValve(index);
    if (!valve) continue;

    // Respect priority: Manual > Timer > Scenario
    // A valve already in SCENARIO source is re-assignable by another scenario
    if (valve->isManuallyControlled() || valve->isTimerControlled()) {
      DEBUG_SCENARIO("Skipping valve %d — overridden by %s\n",
                     valveId,
                     valve->isManuallyControlled() ? "manual" : "timer");
      continue;
    }

    if (strcmp(state, "on") == 0) {
      int duration = action["duration"];  // minutes
      uint32_t durationSec = (uint32_t)duration * 60;
      if (durationSec > MAX_SCENARIO_DURATION_SEC) {
        durationSec = MAX_SCENARIO_DURATION_SEC;
      }
      // Use the timer system for automatic shutoff, then correct the source
      Timers.start(valveId, durationSec);
      Valves.setState(index, true, ValveSource::SCENARIO);
      DEBUG_SCENARIO("Valve %d ON for %d min\n", valveId, duration);
    } else {
      // state == "off"
      Timers.abort(valveId);
      Valves.setState(index, false, ValveSource::NONE);
      DEBUG_SCENARIO("Valve %d OFF\n", valveId);
    }
  }
}
