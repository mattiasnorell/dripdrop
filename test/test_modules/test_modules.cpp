/**
 * Unit tests for ModuleManager.
 *
 * Covers: bus scan (valid/invalid magic, registered flag), registration,
 * removal, sensor reads (ok/error/no-device), and JSON serialization.
 *
 * No real hardware required — Wire responses are programmed via the stub.
 */

#include <unity.h>
#include <ArduinoJson.h>
#include "../../src/modules.h"

// Pull in stub globals (Wire, LittleFS, Mqtt) and the real implementation
#include "../stubs/stubs_common.cpp"
#include "../stubs/stubs_mqtt.cpp"
#include "../../src/modules.cpp"

extern ModuleManager Modules;

// ============================================================================
// Test helpers
// ============================================================================

// Build a packed Descriptor byte array.
static void makeDescriptor(uint8_t* buf,
                            const uint8_t magic[4],
                            uint8_t role,
                            const char* type,
                            uint8_t version,
                            const char* uid) {
  Descriptor d = {};
  memcpy(d.magic, magic, 4);
  d.role = role;
  strncpy(d.type, type, sizeof(d.type) - 1);
  d.version = version;
  strncpy(d.uid,  uid,  sizeof(d.uid)  - 1);
  memcpy(buf, &d, sizeof(Descriptor));
}

// Build a packed SensorResponse byte array.
static void makeSensorResponse(uint8_t* buf, uint8_t status, float value) {
  SensorResponse r;
  r.status = status;
  r.value  = value;
  memcpy(buf, &r, sizeof(SensorResponse));
}

// Shorthand: program a valid Descriptor response at (addr, CMD_GET_DESCRIPTOR).
static void stubDescriptor(uint8_t addr, uint8_t role, const char* type,
                            uint8_t version, const char* uid) {
  uint8_t buf[sizeof(Descriptor)];
  makeDescriptor(buf, MODULE_MAGIC, role, type, version, uid);
  Wire.stub_setResponse(addr, CMD_GET_DESCRIPTOR, buf, sizeof(Descriptor));
}

// ============================================================================
// setUp / tearDown
// ============================================================================

void setUp(void) {
  Wire.stub_reset();
  Modules.begin();  // LittleFS stub returns falsy File → starts with empty lists
}

void tearDown(void) {}

// ============================================================================
// Tests — initialization
// ============================================================================

void test_begin_starts_with_empty_lists(void) {
  TEST_ASSERT_EQUAL(0, Modules.registeredCount());
  TEST_ASSERT_EQUAL(0, Modules.discoveredCount());
}

// ============================================================================
// Tests — scanModules()
// ============================================================================

void test_scan_empty_bus_returns_zero(void) {
  // No Wire responses programmed → all addresses NACK
  TEST_ASSERT_EQUAL(0, Modules.scanModules());
  TEST_ASSERT_EQUAL(0, Modules.discoveredCount());
}

void test_scan_finds_module_with_valid_magic(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  TEST_ASSERT_EQUAL(1, Modules.scanModules());
  TEST_ASSERT_EQUAL(1, Modules.discoveredCount());
}

void test_scan_skips_module_with_bad_magic(void) {
  // Build a descriptor with wrong magic bytes
  uint8_t badMagic[4] = {0x00, 0x11, 0x22, 0x33};
  uint8_t buf[sizeof(Descriptor)];
  makeDescriptor(buf, badMagic, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Wire.stub_setResponse(0x28, CMD_GET_DESCRIPTOR, buf, sizeof(Descriptor));

  TEST_ASSERT_EQUAL(0, Modules.scanModules());
  TEST_ASSERT_EQUAL(0, Modules.discoveredCount());
}

void test_scan_discovers_multiple_modules(void) {
  stubDescriptor(0x10, ROLE_SENSOR, "TMP", 1, "UID000000001");
  stubDescriptor(0x20, ROLE_SENSOR, "HUM", 2, "UID000000002");
  stubDescriptor(0x30, ROLE_DRIVER, "RLY", 1, "UID000000003");

  TEST_ASSERT_EQUAL(3, Modules.scanModules());
  TEST_ASSERT_EQUAL(3, Modules.discoveredCount());
}

void test_scan_newly_discovered_module_is_unregistered(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();

  String json;
  Modules.serializeScan(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_FALSE(doc[0]["registered"].as<bool>());
}

void test_scan_sets_registered_flag_for_known_uid(void) {
  // First scan: discover and register
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();
  Modules.registerModule("AABBCCDDEEFF");

  // Second scan: same module should be flagged as registered
  Wire.stub_reset();
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();

  String json;
  Modules.serializeScan(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_TRUE(doc[0]["registered"].as<bool>());
}

void test_scan_result_contains_correct_fields(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 3, "DEADBEEF1234");
  Modules.scanModules();

  String json;
  Modules.serializeScan(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_EQUAL(0x28,                  doc[0]["addr"].as<int>());
  TEST_ASSERT_EQUAL_STRING("sensor",       doc[0]["role"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("TMP",          doc[0]["type"].as<const char*>());
  TEST_ASSERT_EQUAL(3,                     doc[0]["version"].as<int>());
  TEST_ASSERT_EQUAL_STRING("DEADBEEF1234", doc[0]["uid"].as<const char*>());
}

void test_scan_result_driver_has_driver_role(void) {
  stubDescriptor(0x28, ROLE_DRIVER, "RLY", 1, "DEADBEEF1234");
  Modules.scanModules();

  String json;
  Modules.serializeScan(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_EQUAL_STRING("driver", doc[0]["role"].as<const char*>());
}

// ============================================================================
// Tests — registerModule()
// ============================================================================

void test_register_succeeds_for_discovered_uid(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();

  TEST_ASSERT_TRUE(Modules.registerModule("AABBCCDDEEFF"));
  TEST_ASSERT_EQUAL(1, Modules.registeredCount());
}

void test_register_fails_for_uid_not_in_scan(void) {
  // Scan is empty — nothing to register
  Modules.scanModules();
  TEST_ASSERT_FALSE(Modules.registerModule("DOESNOTEXIST"));
  TEST_ASSERT_EQUAL(0, Modules.registeredCount());
}

void test_register_fails_for_already_registered_uid(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();
  TEST_ASSERT_TRUE(Modules.registerModule("AABBCCDDEEFF"));
  TEST_ASSERT_FALSE(Modules.registerModule("AABBCCDDEEFF"));  // duplicate
  TEST_ASSERT_EQUAL(1, Modules.registeredCount());
}

void test_register_persists_correct_metadata(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 2, "AABBCCDDEEFF");
  Modules.scanModules();
  Modules.registerModule("AABBCCDDEEFF");

  String json;
  Modules.serializeRegistered(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF", doc[0]["uid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("sensor",       doc[0]["role"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("TMP",          doc[0]["type"].as<const char*>());
  TEST_ASSERT_EQUAL(2,                     doc[0]["version"].as<int>());
  TEST_ASSERT_EQUAL(0x28,                  doc[0]["addr"].as<int>());
}

void test_register_driver_persists_driver_role(void) {
  stubDescriptor(0x30, ROLE_DRIVER, "RLY", 1, "DRIVER000001");
  Modules.scanModules();
  Modules.registerModule("DRIVER000001");

  String json;
  Modules.serializeRegistered(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_EQUAL_STRING("driver", doc[0]["role"].as<const char*>());
}

// ============================================================================
// Tests — removeModule()
// ============================================================================

void test_remove_succeeds_for_registered_uid(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();
  Modules.registerModule("AABBCCDDEEFF");

  TEST_ASSERT_TRUE(Modules.removeModule("AABBCCDDEEFF"));
  TEST_ASSERT_EQUAL(0, Modules.registeredCount());
}

void test_remove_fails_for_unknown_uid(void) {
  TEST_ASSERT_FALSE(Modules.removeModule("DOESNOTEXIST"));
}

void test_remove_clears_registered_flag_in_discovered_list(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "AABBCCDDEEFF");
  Modules.scanModules();
  Modules.registerModule("AABBCCDDEEFF");
  Modules.removeModule("AABBCCDDEEFF");

  String json;
  Modules.serializeScan(json);

  JsonDocument doc;
  deserializeJson(doc, json);
  TEST_ASSERT_FALSE(doc[0]["registered"].as<bool>());
}

void test_remove_middle_entry_compacts_list(void) {
  stubDescriptor(0x10, ROLE_SENSOR, "TMP", 1, "UID000000001");
  stubDescriptor(0x20, ROLE_SENSOR, "HUM", 1, "UID000000002");
  stubDescriptor(0x30, ROLE_DRIVER, "RLY", 1, "UID000000003");
  Modules.scanModules();
  Modules.registerModule("UID000000001");
  Modules.registerModule("UID000000002");
  Modules.registerModule("UID000000003");

  Modules.removeModule("UID000000002");

  TEST_ASSERT_EQUAL(2, Modules.registeredCount());

  String json;
  Modules.serializeRegistered(json);
  // UID000000002 must be absent; the other two must still be present
  TEST_ASSERT_NULL(strstr(json.c_str(), "UID000000002"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "UID000000001"));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "UID000000003"));
}

// ============================================================================
// Tests — readModule()
// ============================================================================

void test_read_returns_true_for_ok_status(void) {
  uint8_t buf[sizeof(SensorResponse)];
  makeSensorResponse(buf, 0x00, 23.5f);
  Wire.stub_setResponse(0x28, CMD_GET_READING, buf, sizeof(SensorResponse));

  SensorResponse out;
  TEST_ASSERT_TRUE(Modules.readModule(0x28, out));
  TEST_ASSERT_EQUAL(0x00, out.status);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, out.value);
}

void test_read_returns_false_for_error_status(void) {
  uint8_t buf[sizeof(SensorResponse)];
  makeSensorResponse(buf, 0x01, 0.0f);
  Wire.stub_setResponse(0x28, CMD_GET_READING, buf, sizeof(SensorResponse));

  SensorResponse out;
  TEST_ASSERT_FALSE(Modules.readModule(0x28, out));
}

void test_read_returns_false_when_no_device(void) {
  // No stub response programmed → endTransmission returns NACK
  SensorResponse out;
  TEST_ASSERT_FALSE(Modules.readModule(0x28, out));
}

void test_read_sends_correct_command(void) {
  uint8_t buf[sizeof(SensorResponse)];
  makeSensorResponse(buf, 0x00, 10.0f);
  Wire.stub_setResponse(0x28, CMD_GET_READING, buf, sizeof(SensorResponse));

  SensorResponse out;
  Modules.readModule(0x28, out);

  TEST_ASSERT_EQUAL(0x28,           Wire.stub_lastTxAddr());
  TEST_ASSERT_EQUAL(CMD_GET_READING, Wire.stub_lastTxCmd());
}

// ============================================================================
// Tests — commandModule()
// ============================================================================

void test_command_succeeds_for_registered_driver(void) {
  stubDescriptor(0x28, ROLE_DRIVER, "RLY", 1, "DRIVER000001");
  Modules.scanModules();
  Modules.registerModule("DRIVER000001");

  // ACK the CMD_DO_ACTION command
  Wire.stub_setResponse(0x28, CMD_DO_ACTION, nullptr, 0);

  TEST_ASSERT_TRUE(Modules.commandModule(0x28, 0x01));
}

void test_command_fails_for_sensor(void) {
  stubDescriptor(0x28, ROLE_SENSOR, "TMP", 1, "SENSORUID00001");
  Modules.scanModules();
  Modules.registerModule("SENSORUID00001");

  TEST_ASSERT_FALSE(Modules.commandModule(0x28, 0x01));
}

void test_command_fails_for_unregistered_addr(void) {
  // Address not registered at all
  TEST_ASSERT_FALSE(Modules.commandModule(0x50, 0x01));
}

void test_command_sends_correct_bytes(void) {
  stubDescriptor(0x28, ROLE_DRIVER, "RLY", 1, "DRIVER000001");
  Modules.scanModules();
  Modules.registerModule("DRIVER000001");
  Wire.stub_setResponse(0x28, CMD_DO_ACTION, nullptr, 0);

  Modules.commandModule(0x28, 0x42);

  TEST_ASSERT_EQUAL(0x28,          Wire.stub_lastTxAddr());
  TEST_ASSERT_EQUAL(CMD_DO_ACTION, Wire.stub_lastTxCmd());
  TEST_ASSERT_EQUAL(0x42,          Wire.stub_lastTxPayload());
}

void test_command_fails_when_no_ack(void) {
  stubDescriptor(0x28, ROLE_DRIVER, "RLY", 1, "DRIVER000001");
  Modules.scanModules();
  Modules.registerModule("DRIVER000001");

  // No Wire stub for CMD_DO_ACTION → endTransmission returns NACK
  TEST_ASSERT_FALSE(Modules.commandModule(0x28, 0x01));
}

// ============================================================================
// Tests — serializeRegistered()
// ============================================================================

void test_serialize_registered_empty_returns_empty_array(void) {
  String json;
  Modules.serializeRegistered(json);
  TEST_ASSERT_EQUAL_STRING("[]", json.c_str());
}

void test_serialize_scan_empty_returns_empty_array(void) {
  String json;
  Modules.serializeScan(json);
  TEST_ASSERT_EQUAL_STRING("[]", json.c_str());
}

// ============================================================================
// Test runner
// ============================================================================

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // Initialization
  RUN_TEST(test_begin_starts_with_empty_lists);

  // Scan
  RUN_TEST(test_scan_empty_bus_returns_zero);
  RUN_TEST(test_scan_finds_module_with_valid_magic);
  RUN_TEST(test_scan_skips_module_with_bad_magic);
  RUN_TEST(test_scan_discovers_multiple_modules);
  RUN_TEST(test_scan_newly_discovered_module_is_unregistered);
  RUN_TEST(test_scan_sets_registered_flag_for_known_uid);
  RUN_TEST(test_scan_result_contains_correct_fields);
  RUN_TEST(test_scan_result_driver_has_driver_role);

  // Registration
  RUN_TEST(test_register_succeeds_for_discovered_uid);
  RUN_TEST(test_register_fails_for_uid_not_in_scan);
  RUN_TEST(test_register_fails_for_already_registered_uid);
  RUN_TEST(test_register_persists_correct_metadata);
  RUN_TEST(test_register_driver_persists_driver_role);

  // Removal
  RUN_TEST(test_remove_succeeds_for_registered_uid);
  RUN_TEST(test_remove_fails_for_unknown_uid);
  RUN_TEST(test_remove_clears_registered_flag_in_discovered_list);
  RUN_TEST(test_remove_middle_entry_compacts_list);

  // Reads
  RUN_TEST(test_read_returns_true_for_ok_status);
  RUN_TEST(test_read_returns_false_for_error_status);
  RUN_TEST(test_read_returns_false_when_no_device);
  RUN_TEST(test_read_sends_correct_command);

  // Commands
  RUN_TEST(test_command_succeeds_for_registered_driver);
  RUN_TEST(test_command_fails_for_sensor);
  RUN_TEST(test_command_fails_for_unregistered_addr);
  RUN_TEST(test_command_sends_correct_bytes);
  RUN_TEST(test_command_fails_when_no_ack);

  // Serialization
  RUN_TEST(test_serialize_registered_empty_returns_empty_array);
  RUN_TEST(test_serialize_scan_empty_returns_empty_array);

  return UNITY_END();
}
