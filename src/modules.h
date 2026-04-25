/**
 * DripDrop - Module Manager
 *
 * I²C orchestration layer for Nano-based sensor modules.
 * The ESP32 acts as I²C bus master; Arduino Nanos are slaves at addresses 0x08–0x77.
 *
 * Two-command protocol per module:
 *   0x01 → Nano responds with a Descriptor struct
 *   0x02 → Nano responds with a SensorResponse struct
 *
 * Registered modules are persisted to LittleFS as /modules.json.
 * Schema per entry: { "uid", "type", "version", "unit", "addr" }
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

// =============================================================================
// Wire protocol structs
// Packed to guarantee byte-exact layout for memcpy from I²C read buffer.
// =============================================================================

#pragma pack(push, 1)

struct Descriptor {
  uint8_t magic[4];   // Must equal {0xDE, 0xAD, 0xBE, 0xEF}
  char    type[4];    // Sensor type string, e.g. "TMP" (null-terminated)
  uint8_t version;    // Firmware version of the Nano module
  char    unit[5];    // Measurement unit string, e.g. "C" (null-terminated)
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
   * Serialize the most recent scanModules() results as a JSON array.
   * Fields per entry: addr (int), uid, type, version, unit, registered (bool).
   */
  void serializeScan(String& out) const;

  /**
   * Serialize all registered modules as a JSON array.
   * Fields per entry: uid, type, version, unit, addr (int).
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
    char    type[4];
    uint8_t version;
    char    unit[5];
    uint8_t addr;
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
  bool findDiscovered(const char* uid, uint8_t& outIndex) const;
};

extern ModuleManager Modules;

#endif // DRIPDROP_MODULES_H
