/**
 * Unit tests for TimerManager.
 *
 * Tests start/abort/check lifecycle, duration validation, manual-control
 * override, and query methods (isActive, getRemainingSeconds, get).
 */
#include <unity.h>
#include "../../valves.h"
#include "../../timers.h"
#include "../../sensors.h"

// Include stubs (valve stub) and real timer implementation
#include "../stubs/stubs_common.cpp"
#include "../stubs/stubs_valves.cpp"
#include "../stubs/stubs_sensors.cpp"
#include "../../timers.cpp"

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

void setUp(void) {
  Valves.begin();
  Timers.begin();
}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// start() tests
// ---------------------------------------------------------------------------

void test_start_valid(void) {
  bool ok = Timers.start(1, 600);  // valve 1, 10 minutes
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(Valves.getState(0));  // valve index 0 is on

  Valve* v = Valves.getValve(0);
  TEST_ASSERT_EQUAL(ValveSource::TIMER, v->source);
}

void test_start_invalid_valve_zero(void) {
  bool ok = Timers.start(0, 600);
  TEST_ASSERT_FALSE(ok);
}

void test_start_invalid_valve_too_high(void) {
  bool ok = Timers.start(NUM_VALVES + 1, 600);
  TEST_ASSERT_FALSE(ok);
}

void test_start_duration_zero(void) {
  bool ok = Timers.start(1, 0);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(Valves.getState(0));
}

void test_start_duration_exceeds_max(void) {
  bool ok = Timers.start(1, MAX_TIMER_DURATION_SEC + 1);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(Valves.getState(0));
}

// ---------------------------------------------------------------------------
// check() tests
// ---------------------------------------------------------------------------

void test_check_active_timer_keeps_valve_on(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  // Check while timer is still active
  Timers.check(now + 300);
  TEST_ASSERT_TRUE(Valves.getState(0));
}

void test_check_active_timer_turns_valve_on_if_off(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  // Externally turn valve off (simulating something turning it off)
  Valves.setState(0, false, ValveSource::NONE);
  TEST_ASSERT_FALSE(Valves.getState(0));

  // check() should turn it back on
  Timers.check(now + 100);
  TEST_ASSERT_TRUE(Valves.getState(0));
}

void test_check_expired_timer_turns_valve_off(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  // Check after timer has expired
  Timers.check(now + 601);
  TEST_ASSERT_FALSE(Valves.getState(0));

  // Timer should now be inactive
  TEST_ASSERT_FALSE(Timers.isActive(1, now + 601));
}

void test_check_expired_timer_manual_override(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  // Switch valve to manual control
  Valves.setState(0, true, ValveSource::MANUAL);

  // Timer expires — valve should stay on because it's manually controlled
  Timers.check(now + 601);
  TEST_ASSERT_TRUE(Valves.getState(0));
  TEST_ASSERT_EQUAL(ValveSource::MANUAL, Valves.getValve(0)->source);
}

void test_check_inactive_timer_no_change(void) {
  // No timer started — valve should remain off
  time_t now = time(nullptr);
  Timers.check(now);
  TEST_ASSERT_FALSE(Valves.getState(0));
}

void test_check_multiple_timers_one_expires(void) {
  time_t now = time(nullptr);
  Timers.start(1, 300);   // expires at now+300
  Timers.start(2, 600);   // expires at now+600

  // At now+400: timer 1 expired, timer 2 still active
  Timers.check(now + 400);
  TEST_ASSERT_FALSE(Valves.getState(0));  // valve 1 off
  TEST_ASSERT_TRUE(Valves.getState(1));   // valve 2 still on
}

// ---------------------------------------------------------------------------
// abort() tests
// ---------------------------------------------------------------------------

void test_abort_active_timer(void) {
  Timers.start(1, 600);
  TEST_ASSERT_TRUE(Valves.getState(0));

  bool ok = Timers.abort(1);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FALSE(Valves.getState(0));
}

void test_abort_inactive_timer(void) {
  // No timer started for valve 1
  bool ok = Timers.abort(1);
  TEST_ASSERT_FALSE(ok);
}

void test_abort_invalid_valve(void) {
  bool ok = Timers.abort(0);
  TEST_ASSERT_FALSE(ok);
}

void test_abort_manual_override(void) {
  Timers.start(1, 600);
  // Switch to manual control
  Valves.setState(0, true, ValveSource::MANUAL);

  bool ok = Timers.abort(1);
  TEST_ASSERT_TRUE(ok);
  // Valve should stay on because it's manually controlled
  TEST_ASSERT_TRUE(Valves.getState(0));
  TEST_ASSERT_EQUAL(ValveSource::MANUAL, Valves.getValve(0)->source);
}

// ---------------------------------------------------------------------------
// abortAll() tests
// ---------------------------------------------------------------------------

void test_abort_all_multiple_active(void) {
  Timers.start(1, 600);
  Timers.start(2, 600);
  Timers.start(3, 600);

  Timers.abortAll();

  TEST_ASSERT_FALSE(Valves.getState(0));
  TEST_ASSERT_FALSE(Valves.getState(1));
  TEST_ASSERT_FALSE(Valves.getState(2));
}

void test_abort_all_none_active(void) {
  // Should not crash
  Timers.abortAll();
  TEST_ASSERT_FALSE(Valves.getState(0));
}

// ---------------------------------------------------------------------------
// isActive() / getRemainingSeconds() tests
// ---------------------------------------------------------------------------

void test_is_active_and_remaining(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  TEST_ASSERT_TRUE(Timers.isActive(1, now + 100));
  uint32_t remaining = Timers.getRemainingSeconds(1, now + 100);
  TEST_ASSERT_EQUAL_UINT32(500, remaining);
}

void test_is_active_expired(void) {
  Timers.start(1, 600);
  time_t now = time(nullptr);

  TEST_ASSERT_FALSE(Timers.isActive(1, now + 601));
  TEST_ASSERT_EQUAL_UINT32(0, Timers.getRemainingSeconds(1, now + 601));
}

void test_is_active_invalid_valve(void) {
  TEST_ASSERT_FALSE(Timers.isActive(0, time(nullptr)));
  TEST_ASSERT_EQUAL_UINT32(0, Timers.getRemainingSeconds(0, time(nullptr)));
}

// ---------------------------------------------------------------------------
// get() tests
// ---------------------------------------------------------------------------

void test_get_valid_index(void) {
  Timer* t = Timers.get(0);
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_EQUAL(1, t->valveId);
}

void test_get_out_of_range(void) {
  Timer* t = Timers.get(NUM_VALVES);
  TEST_ASSERT_NULL(t);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // start()
  RUN_TEST(test_start_valid);
  RUN_TEST(test_start_invalid_valve_zero);
  RUN_TEST(test_start_invalid_valve_too_high);
  RUN_TEST(test_start_duration_zero);
  RUN_TEST(test_start_duration_exceeds_max);

  // check()
  RUN_TEST(test_check_active_timer_keeps_valve_on);
  RUN_TEST(test_check_active_timer_turns_valve_on_if_off);
  RUN_TEST(test_check_expired_timer_turns_valve_off);
  RUN_TEST(test_check_expired_timer_manual_override);
  RUN_TEST(test_check_inactive_timer_no_change);
  RUN_TEST(test_check_multiple_timers_one_expires);

  // abort()
  RUN_TEST(test_abort_active_timer);
  RUN_TEST(test_abort_inactive_timer);
  RUN_TEST(test_abort_invalid_valve);
  RUN_TEST(test_abort_manual_override);

  // abortAll()
  RUN_TEST(test_abort_all_multiple_active);
  RUN_TEST(test_abort_all_none_active);

  // isActive() / getRemainingSeconds()
  RUN_TEST(test_is_active_and_remaining);
  RUN_TEST(test_is_active_expired);
  RUN_TEST(test_is_active_invalid_valve);

  // get()
  RUN_TEST(test_get_valid_index);
  RUN_TEST(test_get_out_of_range);

  return UNITY_END();
}
