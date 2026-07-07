/**
 * Unit tests for ScenarioManager.
 *
 * Tests validation, condition evaluation, ID generation, and runtime state
 * through the public interface (add, update, remove, check).
 */
#include <unity.h>
#include <ArduinoJson.h>
#include "../../src/relays.h"
#include "../../src/timers.h"
#include "../../src/scenarios.h"

// Include stubs and implementation directly (native test, no separate compilation)
#include "../stubs/stubs_common.cpp"
#include "../stubs/stubs_relays.cpp"
#include "../stubs/stubs_timers.cpp"
#include "../stubs/stubs_modules.cpp"
#include "../stubs/stubs_mqtt.cpp"
#include "../stubs/stubs_display.cpp"
#include "../../src/scenarios.cpp"

extern void stub_setModuleReading(const char* uid, float value);
extern void stub_resetModuleReadings();

// Shared scenario manager instance (extern from scenarios.cpp)
extern ScenarioManager Scenarios;

// Helper: build a minimal valid scenario JSON
static void buildValidScenario(JsonDocument& doc) {
  doc["name"] = "Test Scenario";
  JsonArray conditions = doc["conditions"].to<JsonArray>();
  JsonObject timeCond = conditions.add<JsonObject>();
  timeCond["type"] = "time";
  timeCond["hour"] = 6;
  timeCond["minute"] = 30;
  JsonArray actions = doc["actions"].to<JsonArray>();
  JsonObject action = actions.add<JsonObject>();
  action["relayId"] = 1;
  action["state"] = "on";
  action["duration"] = 10;
}

// Reset scenario manager to clean state before each test
void setUp(void) {
  // Re-initialize (load() will find no file via stub, starts empty)
  Scenarios.begin();
  Relays.begin();
  stub_resetModuleReadings();
}

void tearDown(void) {
  // Remove all scenarios
  while (Scenarios.count() > 0) {
    // Serialize to get IDs, then remove first
    String output;
    Scenarios.serialize(output);
    JsonDocument doc;
    deserializeJson(doc, output);
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() > 0) {
      const char* id = arr[0]["id"].as<const char*>();
      Scenarios.remove(id);
    } else {
      break;
    }
  }
}

// =============================================================================
// Validation tests (via add())
// =============================================================================

void test_validate_missing_name(void) {
  JsonDocument doc;
  // No name field
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Missing or invalid 'name'", err);
}

void test_validate_missing_conditions(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Missing 'conditions' array", err);
}

void test_validate_empty_conditions(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  doc["conditions"].to<JsonArray>();  // empty array
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("At least one condition is required", err);
}

void test_validate_time_condition_missing_hour(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time";
  c["minute"] = 30;  // no hour
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Time condition requires 'hour' and 'minute'", err);
}

void test_validate_time_condition_hour_out_of_range(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 25; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Invalid time values", err);
}

void test_validate_dayofweek_wrong_length(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "dayOfWeek";
  JsonArray days = c["days"].to<JsonArray>();
  days.add(true); days.add(false); days.add(true);  // only 3 elements
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("dayOfWeek condition requires 'days' array of 7 booleans", err);
}

void test_validate_sensor_invalid_operator(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gte";  // invalid
  c["value"] = 25;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("sensorValue condition requires 'operator' (gt, lt, eq)", err);
}

void test_validate_sensor_missing_value(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1"; c["operator"] = "gt";
  // missing value
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("sensorValue condition requires 'value'", err);
}

void test_validate_unknown_condition_type(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "weather";
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Unknown condition type", err);
}

void test_validate_missing_actions(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  // no actions

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Missing 'actions' array", err);
}

void test_validate_empty_actions(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  doc["actions"].to<JsonArray>();  // empty

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("At least one action is required", err);
}

void test_validate_action_invalid_relay(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 99;  // invalid
  a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Invalid relayId in action", err);
}

void test_validate_action_on_missing_duration(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on";
  // missing duration

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Action with state 'on' requires positive 'duration'", err);
}

void test_validate_action_off_no_duration_needed(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "off";
  // no duration — should be fine for "off"

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NULL(err);
}

void test_validate_full_valid_scenario(void) {
  JsonDocument doc;
  buildValidScenario(doc);

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL(1, Scenarios.count());
}

void test_validate_condition_missing_type(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  // no "type" field
  c["hour"] = 6;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Condition missing 'type'", err);
}

void test_validate_action_invalid_state(void) {
  JsonDocument doc;
  doc["name"] = "Test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 6; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "toggle"; a["duration"] = 10;

  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Action requires 'state' (on or off)", err);
}

// =============================================================================
// ID generation tests (via add())
// =============================================================================

void test_nextid_first_scenario_gets_id_1(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  String id;
  Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_EQUAL_STRING("1", id.c_str());
}

void test_nextid_sequential(void) {
  JsonDocument doc1, doc2, doc3;
  buildValidScenario(doc1);
  buildValidScenario(doc2);
  buildValidScenario(doc3);
  String id1, id2, id3;
  Scenarios.add(doc1.as<JsonObject>(), id1);
  Scenarios.add(doc2.as<JsonObject>(), id2);
  Scenarios.add(doc3.as<JsonObject>(), id3);
  TEST_ASSERT_EQUAL_STRING("1", id1.c_str());
  TEST_ASSERT_EQUAL_STRING("2", id2.c_str());
  TEST_ASSERT_EQUAL_STRING("3", id3.c_str());
}

void test_nextid_after_delete_uses_max_plus_one(void) {
  JsonDocument doc1, doc2, doc3;
  buildValidScenario(doc1);
  buildValidScenario(doc2);
  buildValidScenario(doc3);
  String id1, id2, id3;
  Scenarios.add(doc1.as<JsonObject>(), id1);
  Scenarios.add(doc2.as<JsonObject>(), id2);
  // Delete id 2
  Scenarios.remove(id2.c_str());
  // Next ID should be 3 (max=1, so 1+1=2? No, we deleted 2, max remaining is 1)
  // Wait: after add(1), add(2), remove(2), remaining IDs are [1]. max=1, next=2.
  Scenarios.add(doc3.as<JsonObject>(), id3);
  TEST_ASSERT_EQUAL_STRING("2", id3.c_str());
}

void test_max_scenarios_limit(void) {
  // Fill to MAX_SCENARIOS
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    JsonDocument doc;
    buildValidScenario(doc);
    String id;
    const char* err = Scenarios.add(doc.as<JsonObject>(), id);
    TEST_ASSERT_NULL_MESSAGE(err, "Should accept up to MAX_SCENARIOS");
  }
  TEST_ASSERT_EQUAL(MAX_SCENARIOS, Scenarios.count());

  // One more should fail
  JsonDocument doc;
  buildValidScenario(doc);
  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Maximum number of scenarios reached", err);
}

// =============================================================================
// findIndex tests (via update/remove)
// =============================================================================

void test_update_nonexistent_scenario(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  const char* err = Scenarios.update("999", doc.as<JsonObject>());
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("Scenario not found", err);
}

void test_remove_nonexistent_scenario(void) {
  TEST_ASSERT_FALSE(Scenarios.remove("999"));
}

void test_remove_null_id(void) {
  TEST_ASSERT_FALSE(Scenarios.remove(nullptr));
}

void test_update_existing_scenario(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  String id;
  Scenarios.add(doc.as<JsonObject>(), id);

  JsonDocument updated;
  buildValidScenario(updated);
  updated["name"] = "Updated Name";
  const char* err = Scenarios.update(id.c_str(), updated.as<JsonObject>());
  TEST_ASSERT_NULL(err);
  TEST_ASSERT_EQUAL(1, Scenarios.count());
}

// =============================================================================
// Condition evaluation tests (via check())
// =============================================================================

// Helper: create a scenario with specific conditions and add it
static String addScenarioWithConditions(JsonDocument& doc) {
  String id;
  Scenarios.add(doc.as<JsonObject>(), id);
  return id;
}

// Helper: build a struct tm for a specific time
// April 12, 2026 is a Sunday (wday=0), so April 12+wday gives the desired day.
static struct tm makeTime(int hour, int minute, int wday) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_year = 126;  // 2026
  t.tm_mon = 3;     // April
  t.tm_mday = 12 + wday;  // April 12=Sun, 13=Mon, ..., 18=Sat
  t.tm_isdst = -1;
  return t;
}

// Helper: convert tm to time_t
static time_t tmToTime(struct tm* t) {
  return mktime(t);
}

void test_eval_time_matches(void) {
  JsonDocument doc;
  doc["name"] = "Time test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 14; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  addScenarioWithConditions(doc);

  // Call check at 14:30
  struct tm t = makeTime(14, 30, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  // Relay 1 should be on (scenario fired)
  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_time_no_match(void) {
  JsonDocument doc;
  doc["name"] = "Time test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "time"; c["hour"] = 14; c["minute"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 2; a["state"] = "on"; a["duration"] = 10;

  addScenarioWithConditions(doc);

  // Call check at 15:00 — should NOT fire
  struct tm t = makeTime(15, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_FALSE(Relays.getState(1));
}

void test_eval_dayofweek_matches(void) {
  JsonDocument doc;
  doc["name"] = "Day test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "dayOfWeek";
  JsonArray days = c["days"].to<JsonArray>();
  // Sun=false, Mon=true, Tue-Sat=false
  days.add(false); days.add(true); days.add(false); days.add(false);
  days.add(false); days.add(false); days.add(false);
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  // Monday = wday 1
  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_dayofweek_no_match(void) {
  JsonDocument doc;
  doc["name"] = "Day test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "dayOfWeek";
  JsonArray days = c["days"].to<JsonArray>();
  // Only Monday enabled
  days.add(false); days.add(true); days.add(false); days.add(false);
  days.add(false); days.add(false); days.add(false);
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 3; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  // Tuesday = wday 2
  struct tm t = makeTime(12, 0, 2);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_FALSE(Relays.getState(2));
}

void test_eval_sensor_gt_passes(void) {
  JsonDocument doc;
  doc["name"] = "Sensor gt";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 25;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  stub_setModuleReading("temp1", 30.0f);  // 30 > 25

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_sensor_gt_fails(void) {
  JsonDocument doc;
  doc["name"] = "Sensor gt fail";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 25;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 2; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  stub_setModuleReading("temp1", 20.0f);  // 20 !> 25

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_FALSE(Relays.getState(1));
}

void test_eval_sensor_lt_passes(void) {
  JsonDocument doc;
  doc["name"] = "Sensor lt";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "soil1";
  c["operator"] = "lt"; c["value"] = 30;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  stub_setModuleReading("soil1", 15.0f);  // 15 < 30

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_sensor_eq_passes(void) {
  JsonDocument doc;
  doc["name"] = "Sensor eq";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "eq"; c["value"] = 22;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  stub_setModuleReading("temp1", 22.0f);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_sensor_unavailable_returns_false(void) {
  JsonDocument doc;
  doc["name"] = "Sensor unavailable";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 0;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 4; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  // Don't register the module — addrForUid returns 0, condition fails

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_FALSE(Relays.getState(3));
}

void test_eval_and_logic_all_pass(void) {
  JsonDocument doc;
  doc["name"] = "AND test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  // Time condition
  JsonObject c1 = conds.add<JsonObject>();
  c1["type"] = "time"; c1["hour"] = 6; c1["minute"] = 30;
  // Day condition (Monday)
  JsonObject c2 = conds.add<JsonObject>();
  c2["type"] = "dayOfWeek";
  JsonArray days = c2["days"].to<JsonArray>();
  days.add(false); days.add(true); days.add(false); days.add(false);
  days.add(false); days.add(false); days.add(false);

  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 10;

  addScenarioWithConditions(doc);

  // 06:30 on Monday
  struct tm t = makeTime(6, 30, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_eval_and_logic_one_fails(void) {
  JsonDocument doc;
  doc["name"] = "AND fail";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  // Time condition
  JsonObject c1 = conds.add<JsonObject>();
  c1["type"] = "time"; c1["hour"] = 6; c1["minute"] = 30;
  // Day condition (Monday only)
  JsonObject c2 = conds.add<JsonObject>();
  c2["type"] = "dayOfWeek";
  JsonArray days = c2["days"].to<JsonArray>();
  days.add(false); days.add(true); days.add(false); days.add(false);
  days.add(false); days.add(false); days.add(false);

  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 3; a["state"] = "on"; a["duration"] = 10;

  addScenarioWithConditions(doc);

  // 06:30 on Tuesday — time matches, day doesn't
  struct tm t = makeTime(6, 30, 2);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  TEST_ASSERT_FALSE(Relays.getState(2));
}

// =============================================================================
// Edge detection / debounce tests (via check())
// =============================================================================

void test_edge_detection_fires_once(void) {
  JsonDocument doc;
  doc["name"] = "Edge test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);
  stub_setModuleReading("temp1", 25.0f);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);

  // First check — should fire
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));

  // Reset relay to verify it doesn't fire again
  Relays.setState(0, false, RelaySource::NONE);

  // Second check at same conditions — should NOT fire (already fired)
  Scenarios.check(ts);
  TEST_ASSERT_FALSE(Relays.getState(0));
}

void test_edge_detection_resets_when_conditions_change(void) {
  JsonDocument doc;
  doc["name"] = "Edge reset";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);

  // Fire once
  stub_setModuleReading("temp1", 25.0f);
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));

  // Conditions no longer match
  stub_setModuleReading("temp1", 15.0f);
  Relays.setState(0, false, RelaySource::NONE);
  Scenarios.check(ts);

  // Conditions match again — should fire again
  stub_setModuleReading("temp1", 25.0f);
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));
}

// =============================================================================
// Serialize / lastRun tests
// =============================================================================

void test_serialize_includes_lastrun_null(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  String id;
  Scenarios.add(doc.as<JsonObject>(), id);

  String output;
  Scenarios.serialize(output);

  JsonDocument result;
  deserializeJson(result, output);
  JsonArray arr = result.as<JsonArray>();
  TEST_ASSERT_EQUAL(1, arr.size());
  TEST_ASSERT_TRUE(arr[0]["lastRun"].isNull());
}

void test_serialize_includes_lastrun_after_fire(void) {
  JsonDocument doc;
  doc["name"] = "LastRun test";
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 0;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  String id;
  Scenarios.add(doc.as<JsonObject>(), id);

  stub_setModuleReading("temp1", 10.0f);
  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  String output;
  Scenarios.serialize(output);

  JsonDocument result;
  deserializeJson(result, output);
  JsonArray arr = result.as<JsonArray>();
  TEST_ASSERT_FALSE(arr[0]["lastRun"].isNull());
  TEST_ASSERT_EQUAL(ts, arr[0]["lastRun"].as<long>());
}

// =============================================================================
// Repeat interval tests (via check())
// =============================================================================

void test_repeat_refires_after_interval(void) {
  JsonDocument doc;
  doc["name"] = "Repeat test";
  doc["repeatInterval"] = 60;
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);
  stub_setModuleReading("temp1", 25.0f);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);

  // First check — fires
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));

  // Before the interval elapses — should NOT re-fire
  Relays.setState(0, false, RelaySource::NONE);
  Scenarios.check(ts + 59);
  TEST_ASSERT_FALSE(Relays.getState(0));

  // Interval elapsed, conditions still true — re-fires
  Scenarios.check(ts + 60);
  TEST_ASSERT_TRUE(Relays.getState(0));
}

void test_repeat_zero_behaves_as_fire_once(void) {
  JsonDocument doc;
  doc["name"] = "No repeat";
  doc["repeatInterval"] = 0;
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);
  stub_setModuleReading("temp1", 25.0f);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);

  // Fires once
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));

  // Long after — still must not re-fire (conditions never dropped)
  Relays.setState(0, false, RelaySource::NONE);
  Scenarios.check(ts + 3600);
  TEST_ASSERT_FALSE(Relays.getState(0));
}

void test_validate_repeat_interval_negative(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  doc["repeatInterval"] = -1;
  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
}

// =============================================================================
// IsActive (enable flag) tests
// =============================================================================

void test_validate_isactive_non_boolean_rejected(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  doc["IsActive"] = "yes";  // not a boolean
  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NOT_NULL(err);
  TEST_ASSERT_EQUAL_STRING("IsActive must be a boolean", err);
}

void test_isactive_defaults_true_when_absent(void) {
  JsonDocument doc;
  buildValidScenario(doc);  // no IsActive field
  String id;
  Scenarios.add(doc.as<JsonObject>(), id);

  String output;
  Scenarios.serialize(output);
  JsonDocument result;
  deserializeJson(result, output);
  JsonArray arr = result.as<JsonArray>();
  TEST_ASSERT_EQUAL(1, arr.size());
  TEST_ASSERT_TRUE(arr[0]["IsActive"].is<bool>());
  TEST_ASSERT_TRUE(arr[0]["IsActive"].as<bool>());
}

void test_isactive_roundtrips_false(void) {
  JsonDocument doc;
  buildValidScenario(doc);
  doc["IsActive"] = false;
  String id;
  const char* err = Scenarios.add(doc.as<JsonObject>(), id);
  TEST_ASSERT_NULL(err);

  String output;
  Scenarios.serialize(output);
  JsonDocument result;
  deserializeJson(result, output);
  JsonArray arr = result.as<JsonArray>();
  TEST_ASSERT_FALSE(arr[0]["IsActive"].as<bool>());
}

void test_inactive_scenario_does_not_fire(void) {
  JsonDocument doc;
  doc["name"] = "Inactive";
  doc["IsActive"] = false;
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  addScenarioWithConditions(doc);
  stub_setModuleReading("temp1", 25.0f);  // conditions would match

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);
  Scenarios.check(ts);

  // Disabled — must not fire even though conditions are true
  TEST_ASSERT_FALSE(Relays.getState(0));
}

void test_reenabling_scenario_fires_on_next_edge(void) {
  JsonDocument doc;
  doc["name"] = "Toggle enable";
  doc["IsActive"] = false;
  JsonArray conds = doc["conditions"].to<JsonArray>();
  JsonObject c = conds.add<JsonObject>();
  c["type"] = "sensorValue"; c["sensorId"] = "temp1";
  c["operator"] = "gt"; c["value"] = 20;
  JsonArray acts = doc["actions"].to<JsonArray>();
  JsonObject a = acts.add<JsonObject>();
  a["relayId"] = 1; a["state"] = "on"; a["duration"] = 5;

  String id;
  Scenarios.add(doc.as<JsonObject>(), id);
  stub_setModuleReading("temp1", 25.0f);

  struct tm t = makeTime(12, 0, 1);
  time_t ts = tmToTime(&t);

  // Disabled — no fire
  Scenarios.check(ts);
  TEST_ASSERT_FALSE(Relays.getState(0));

  // Re-enable via update
  JsonDocument upd;
  upd["name"] = "Toggle enable";
  upd["IsActive"] = true;
  JsonArray uconds = upd["conditions"].to<JsonArray>();
  JsonObject uc = uconds.add<JsonObject>();
  uc["type"] = "sensorValue"; uc["sensorId"] = "temp1";
  uc["operator"] = "gt"; uc["value"] = 20;
  JsonArray uacts = upd["actions"].to<JsonArray>();
  JsonObject ua = uacts.add<JsonObject>();
  ua["relayId"] = 1; ua["state"] = "on"; ua["duration"] = 5;
  const char* err = Scenarios.update(id.c_str(), upd.as<JsonObject>());
  TEST_ASSERT_NULL(err);

  // Now active — fires
  Scenarios.check(ts);
  TEST_ASSERT_TRUE(Relays.getState(0));
}

// =============================================================================
// Main
// =============================================================================

int main() {
  UNITY_BEGIN();

  // Validation
  RUN_TEST(test_validate_missing_name);
  RUN_TEST(test_validate_missing_conditions);
  RUN_TEST(test_validate_empty_conditions);
  RUN_TEST(test_validate_time_condition_missing_hour);
  RUN_TEST(test_validate_time_condition_hour_out_of_range);
  RUN_TEST(test_validate_dayofweek_wrong_length);
  RUN_TEST(test_validate_sensor_invalid_operator);
  RUN_TEST(test_validate_sensor_missing_value);
  RUN_TEST(test_validate_unknown_condition_type);
  RUN_TEST(test_validate_missing_actions);
  RUN_TEST(test_validate_empty_actions);
  RUN_TEST(test_validate_action_invalid_relay);
  RUN_TEST(test_validate_action_on_missing_duration);
  RUN_TEST(test_validate_action_off_no_duration_needed);
  RUN_TEST(test_validate_full_valid_scenario);
  RUN_TEST(test_validate_condition_missing_type);
  RUN_TEST(test_validate_action_invalid_state);

  // ID generation
  RUN_TEST(test_nextid_first_scenario_gets_id_1);
  RUN_TEST(test_nextid_sequential);
  RUN_TEST(test_nextid_after_delete_uses_max_plus_one);
  RUN_TEST(test_max_scenarios_limit);

  // findIndex (via update/remove)
  RUN_TEST(test_update_nonexistent_scenario);
  RUN_TEST(test_remove_nonexistent_scenario);
  RUN_TEST(test_remove_null_id);
  RUN_TEST(test_update_existing_scenario);

  // Condition evaluation
  RUN_TEST(test_eval_time_matches);
  RUN_TEST(test_eval_time_no_match);
  RUN_TEST(test_eval_dayofweek_matches);
  RUN_TEST(test_eval_dayofweek_no_match);
  RUN_TEST(test_eval_sensor_gt_passes);
  RUN_TEST(test_eval_sensor_gt_fails);
  RUN_TEST(test_eval_sensor_lt_passes);
  RUN_TEST(test_eval_sensor_eq_passes);
  RUN_TEST(test_eval_sensor_unavailable_returns_false);
  RUN_TEST(test_eval_and_logic_all_pass);
  RUN_TEST(test_eval_and_logic_one_fails);

  // Edge detection
  RUN_TEST(test_edge_detection_fires_once);
  RUN_TEST(test_edge_detection_resets_when_conditions_change);

  // Repeat interval
  RUN_TEST(test_repeat_refires_after_interval);
  RUN_TEST(test_repeat_zero_behaves_as_fire_once);
  RUN_TEST(test_validate_repeat_interval_negative);

  // IsActive enable flag
  RUN_TEST(test_validate_isactive_non_boolean_rejected);
  RUN_TEST(test_isactive_defaults_true_when_absent);
  RUN_TEST(test_isactive_roundtrips_false);
  RUN_TEST(test_inactive_scenario_does_not_fire);
  RUN_TEST(test_reenabling_scenario_fires_on_next_edge);

  // Serialize
  RUN_TEST(test_serialize_includes_lastrun_null);
  RUN_TEST(test_serialize_includes_lastrun_after_fire);

  return UNITY_END();
}
