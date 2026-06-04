/**
 * DripDrop - Module Manager
 *
 * I²C orchestration layer for Nano-based sensor and driver modules.
 * The ESP32 acts as I²C bus master; Arduino Nanos are slaves at addresses 0x08–0x77.
 *
 * Three-command protocol:
 *   0x01 → Nano responds with a Descriptor struct (all modules)
 *   0x02 → Nano responds with a SensorResponse struct (sensors only)
 *   0x03 → Nano receives an action byte, no response expected (drivers only)
 *
 * Registered modules are persisted to LittleFS as /modules.json.
 * Schema per entry: { "uid", "role", "type", "version", "addr", "customName", "unit" }
 */

#ifndef DRIPDROP_MODULES_H
#define DRIPDROP_MODULES_H

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "config.h"

// LittleFS path for registered module storage
constexpr const char* MODULES_FILE = "/modules.json";

// I²C address scan range (0x00–0x07 and 0x78–0x7F are reserved)
constexpr uint8_t I2C_ADDR_MIN = 0x08;
constexpr uint8_t I2C_ADDR_MAX = 0x77;

// Expected magic bytes in every Descriptor
constexpr uint8_t MODULE_MAGIC[4] = {0xDE, 0xAD, 0xBE, 0xEF};

// Wire protocol commands
constexpr uint8_t CMD_GET_DESCRIPTOR = 0x01;
constexpr uint8_t CMD_GET_READING    = 0x02;
constexpr uint8_t CMD_DO_ACTION      = 0x03;

// Module roles
constexpr uint8_t ROLE_SENSOR = 0x00;
constexpr uint8_t ROLE_DRIVER = 0x01;

// =============================================================================
// Wire protocol structs
// Packed to guarantee byte-exact layout for memcpy from I²C read buffer.
// =============================================================================

#pragma pack(push, 1)

struct Descriptor {
  uint8_t magic[4];   // Must equal {0xDE, 0xAD, 0xBE, 0xEF}
  uint8_t role;       // ROLE_SENSOR or ROLE_DRIVER
  char    type[4];    // Module type string, e.g. "TMP", "RLY" (null-terminated)
  uint8_t version;    // Firmware version of the Nano module
  char    uid[13];    // Unique ID burned into Nano EEPROM (null-terminated)
};

struct SensorResponse {
  uint8_t status;     // 0x00 = ok, 0x01 = error
  float   value;      // Sensor reading (valid only when status == 0x00)
};

#pragma pack(pop)

// =============================================================================
// ModuleManager
// =============================================================================

class ModuleManager {
public:
  /**
   * Initialize I²C bus and load registered modules from LittleFS.
   * Call once in setup().
   */
  void begin();

  /**
   * Scan I²C addresses 0x08–0x77. For each responding address, send
   * CMD_GET_DESCRIPTOR and validate the magic bytes. Each discovered module
   * is flagged as registered or unregistered by comparing its UID against
   * the stored registered-module list.
   *
   * The per-address probe is isolated in scanAddress() so a TCA9548A mux
   * can be added later without changing any other code.
   *
   * @return Number of modules found with valid magic bytes.
   */
  uint8_t scanModules();

  /**
   * Persist a discovered module to LittleFS by UID.
   * The module must appear in the most recent scanModules() result.
   *
   * @param uid  UID of the module to register.
   * @return true on success; false if UID was not in the last scan,
   *         is already registered, or the module list is full.
   */
  bool registerModule(const char* uid);

  /**
   * Remove a registered module from LittleFS by UID.
   *
   * @param uid  UID of the module to remove.
   * @return true on success; false if the UID is not registered.
   */
  bool removeModule(const char* uid);

  /**
   * Set a custom name for a registered module and persist to flash.
   * Pass nullptr or empty string to clear. Returns false if uid is not registered.
   *
   * @param uid   UID of the module to name.
   * @param name  Display name (max 31 chars), or nullptr/empty to clear.
   * @return true on success; false if the UID is not registered.
   */
  bool setCustomName(const char* uid, const char* name);

  /**
   * Set the unit label for a registered module and persist to flash.
   * Unit is user-defined (not derived from hardware). Pass nullptr or empty to clear.
   *
   * @param uid   UID of the module.
   * @param unit  Unit string (max 4 chars), or nullptr/empty to clear.
   * @return true on success; false if the UID is not registered.
   */
  bool setUnit(const char* uid, const char* unit);

  /**
   * Send CMD_GET_READING to addr and read a SensorResponse.
   * Returns false without populating out if the I²C transfer fails or
   * the module reports status != 0x00.
   *
   * @param addr  I²C address to query.
   * @param out   Populated with the response on success.
   * @return true if status == 0x00; false on I²C error or sensor error.
   */
  bool readModule(uint8_t addr, SensorResponse& out);

  /**
   * Send CMD_DO_ACTION + cmd to a driver module at addr. Fire-and-forget —
   * only the I²C ACK is checked. Returns false if addr is not a registered
   * driver or the I²C transmission is not acknowledged.
   *
   * @param addr  I²C address of the driver module.
   * @param cmd   Action byte interpreted by the module firmware.
   * @return true on ACK; false on NACK or role mismatch.
   */
  bool commandModule(uint8_t addr, uint8_t cmd);

  /**
   * Serialize the most recent scanModules() results as a JSON array.
   * Fields per entry: addr (int), uid, role, type, version, registered (bool).
   */
  void serializeScan(String& out) const;

  /**
   * Serialize all registered modules as a JSON array.
   * Fields per entry: uid, role, type, version, addr (int), customName, unit.
   */
  void serializeRegistered(String& out) const;

  /**
   * Return the I²C address of a registered module by UID.
   * @return addr if found; 0 if not registered.
   */
  uint8_t addrForUid(const char* uid) const;

  uint8_t discoveredCount()  const { return _discoveredCount; }
  uint8_t registeredCount()  const { return _registeredCount; }

private:
  // ---- Internal representations ----------------------------------------

  struct RegisteredModule {
    char    uid[13];
    uint8_t role;
    char    type[4];
    uint8_t version;
    uint8_t addr;
    char    customName[32];
    char    unit[5];        // user-defined label, not from hardware
  };

  struct DiscoveredModule {
    uint8_t    addr;
    Descriptor desc;
    bool       registered;
  };

  RegisteredModule _registered[MAX_MODULES];
  uint8_t          _registeredCount = 0;

  DiscoveredModule _discovered[MAX_MODULES];
  uint8_t          _discoveredCount = 0;

  // ---- Isolated scan logic ---------------------------------------------

  /**
   * Send CMD_GET_DESCRIPTOR to a single I²C address and read the response.
   * Keeping this in its own function lets a TCA9548A mux be spliced in later
   * (select the right channel, call scanAddress, deselect) without touching
   * any other code path.
   *
   * @param addr  I²C address to probe.
   * @param desc  Populated on success.
   * @return true if the full Descriptor was read successfully.
   */
  bool scanAddress(uint8_t addr, Descriptor& desc);

  // ---- LittleFS helpers -----------------------------------------------

  void load();
  void save();

  // ---- Lookup helpers -------------------------------------------------

  bool findRegistered(const char* uid, uint8_t& outIndex) const;
  bool findRegisteredByAddr(uint8_t addr, uint8_t& outIndex) const;
  bool findDiscovered(const char* uid, uint8_t& outIndex) const;
};

extern ModuleManager Modules;

#endif // DRIPDROP_MODULES_H
