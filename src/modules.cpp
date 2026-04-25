/**
 * DripDrop - Module Manager Implementation
 */

#include "modules.h"
#include "logger.h"
#include <LittleFS.h>

ModuleManager Modules;

// ============================================================================
// Public API
// ============================================================================

void ModuleManager::begin()
{
  Wire.begin();
  Wire.setTimeout(100); // 100 ms cap — prevents watchdog stalls from absent modules
  _discoveredCount = 0; // Clear any previous scan results
  DEBUG_PRINTLN(F("[MODULE] I2C bus initialized"));
  load();
}

uint8_t ModuleManager::scanModules()
{
  _discoveredCount = 0;

  for (uint8_t addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX; addr++)
  {
    if (_discoveredCount >= MAX_MODULES)
      break;

    Descriptor desc;
    if (!scanAddress(addr, desc))
      continue;

    // Validate magic bytes — skip any non-DripDrop device on the bus
    if (memcmp(desc.magic, MODULE_MAGIC, 4) != 0)
    {
      DEBUG_PRINTF("[MODULE] 0x%02X: bad magic bytes, skipping\n", addr);
      continue;
    }

    // Force null-termination on string fields (defensive against malformed firmware)
    desc.type[3] = '\0';
    desc.unit[4] = '\0';
    desc.uid[12] = '\0';

    DiscoveredModule &slot = _discovered[_discoveredCount++];
    slot.addr = addr;
    slot.desc = desc;

    uint8_t regIdx;
    slot.registered = findRegistered(desc.uid, regIdx);

    DEBUG_PRINTF("[MODULE] 0x%02X: found %s uid=%s %s\n",
                 addr, desc.type, desc.uid, slot.registered ? "(registered)" : "(unregistered)");
  }

  return _discoveredCount;
}

bool ModuleManager::registerModule(const char *uid)
{
  uint8_t discIdx;
  if (!findDiscovered(uid, discIdx))
  {
    DEBUG_PRINTF("[MODULE] registerModule: uid=%s not in last scan\n", uid);

    char det[96];
    snprintf(det, sizeof(det), "{\"uid\":\"%s\",\"message\":\"uid=%s not in last scan\"}", uid, uid);
    Logger.Warning(LogEvent::MODULE_REGISTRATION, det);
    return false;
  }

  uint8_t regIdx;
  if (findRegistered(uid, regIdx))
  {
    DEBUG_PRINTF("[MODULE] registerModule: uid=%s already registered\n", uid);
    char det[96];
    snprintf(det, sizeof(det), "{\"uid\":\"%s\",\"message\":\"uid=%s already registered\"}", uid, uid);
    Logger.Warning(LogEvent::MODULE_REGISTRATION, det);

    return false;
  }

  if (_registeredCount >= MAX_MODULES)
  {
    DEBUG_PRINTLN(F("[MODULE] registerModule: registered module list full"));

    char det[96];
    snprintf(det, sizeof(det), "{\"uid\":\"%s\",\"message\":\"registered module list full\"}", uid);
    Logger.Warning(LogEvent::MODULE_REGISTRATION, det);
    return false;
  }

  const Descriptor &desc = _discovered[discIdx].desc;
  RegisteredModule &m = _registered[_registeredCount++];

  strncpy(m.uid, desc.uid, sizeof(m.uid) - 1);
  m.uid[sizeof(m.uid) - 1] = '\0';
  strncpy(m.type, desc.type, sizeof(m.type) - 1);
  m.type[sizeof(m.type) - 1] = '\0';
  strncpy(m.unit, desc.unit, sizeof(m.unit) - 1);
  m.unit[sizeof(m.unit) - 1] = '\0';
  m.version = desc.version;
  m.addr = _discovered[discIdx].addr;

  _discovered[discIdx].registered = true;

  save();
  DEBUG_PRINTF("[MODULE] Registered uid=%s addr=0x%02X\n", m.uid, m.addr);

  char det[96];
  snprintf(det, sizeof(det), "{\"uid\":\"%s\",\"address\":\"0x%02X\",\"message\":\"Module registered\"}", uid, m.addr);
  Logger.Info(LogEvent::MODULE_REGISTRATION, det);

  return true;
}

bool ModuleManager::removeModule(const char *uid)
{
  uint8_t idx;
  if (!findRegistered(uid, idx))
  {
    DEBUG_PRINTF("[MODULE] removeModule: uid=%s not found\n", uid);
    return false;
  }

  // Compact the array by shifting remaining entries down one slot
  for (uint8_t i = idx; i < _registeredCount - 1; i++)
  {
    _registered[i] = _registered[i + 1];
  }
  _registeredCount--;

  // Mirror removal in the discovered list so serializeScan() stays consistent
  for (uint8_t i = 0; i < _discoveredCount; i++)
  {
    if (strcmp(_discovered[i].desc.uid, uid) == 0)
    {
      _discovered[i].registered = false;
      break;
    }
  }

  save();
  DEBUG_PRINTF("[MODULE] Removed uid=%s\n", uid);

  char det[96];
  snprintf(det, sizeof(det), "{\"uid\":\"%s\"}", uid);
  Logger.Info(LogEvent::MODULE_REMOVED, det);

  return true;
}

bool ModuleManager::readModule(uint8_t addr, SensorResponse &out)
{
  Wire.beginTransmission(addr);
  Wire.write(CMD_GET_READING);
  if (Wire.endTransmission() != 0)
  {
    DEBUG_PRINTF("[MODULE] readModule: no ACK from 0x%02X\n", addr);
    return false;
  }

  uint8_t bytesRead = Wire.requestFrom(addr, (uint8_t)sizeof(SensorResponse));
  if (bytesRead != sizeof(SensorResponse))
  {
    DEBUG_PRINTF("[MODULE] readModule: short read from 0x%02X (%d/%d bytes)\n",
                 addr, bytesRead, (int)sizeof(SensorResponse));
    return false;
  }

  uint8_t buf[sizeof(SensorResponse)];
  for (uint8_t i = 0; i < sizeof(SensorResponse); i++)
  {
    buf[i] = (uint8_t)Wire.read();
  }
  memcpy(&out, buf, sizeof(SensorResponse));

  if (out.status != 0x00)
  {
    DEBUG_PRINTF("[MODULE] readModule: sensor error (status=0x%02X) from 0x%02X\n",
                 out.status, addr);
    return false;
  }

  return true;
}

uint8_t ModuleManager::addrForUid(const char *uid) const
{
  uint8_t idx;
  return findRegistered(uid, idx) ? _registered[idx].addr : 0;
}

void ModuleManager::serializeScan(String &out) const
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < _discoveredCount; i++)
  {
    const DiscoveredModule &m = _discovered[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["addr"] = m.addr;
    obj["uid"] = m.desc.uid;
    obj["type"] = m.desc.type;
    obj["version"] = m.desc.version;
    obj["unit"] = m.desc.unit;
    obj["registered"] = m.registered;
  }

  serializeJson(doc, out);
}

void ModuleManager::serializeRegistered(String &out) const
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < _registeredCount; i++)
  {
    const RegisteredModule &m = _registered[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["uid"] = m.uid;
    obj["type"] = m.type;
    obj["version"] = m.version;
    obj["unit"] = m.unit;
    obj["addr"] = m.addr;
  }

  serializeJson(doc, out);
}

// ============================================================================
// Private: Isolated scan logic
// ============================================================================

bool ModuleManager::scanAddress(uint8_t addr, Descriptor &desc)
{
  Wire.beginTransmission(addr);
  Wire.write(CMD_GET_DESCRIPTOR);
  if (Wire.endTransmission() != 0)
    return false; // No device at this address

  uint8_t bytesRead = Wire.requestFrom(addr, (uint8_t)sizeof(Descriptor));
  if (bytesRead != sizeof(Descriptor))
    return false;

  uint8_t buf[sizeof(Descriptor)];
  for (uint8_t i = 0; i < sizeof(Descriptor); i++)
  {
    buf[i] = (uint8_t)Wire.read();
  }
  memcpy(&desc, buf, sizeof(Descriptor));
  return true;
}

// ============================================================================
// Private: LittleFS persistence
// ============================================================================

void ModuleManager::load()
{
  _registeredCount = 0;

  File f = LittleFS.open(MODULES_FILE, "r");
  if (!f)
  {
    DEBUG_PRINTLN(F("[MODULE] modules.json not found, starting empty"));
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err)
  {
    DEBUG_PRINTF("[MODULE] modules.json parse error: %s\n", err.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject entry : arr)
  {
    if (_registeredCount >= MAX_MODULES)
      break;

    const char *uid = entry["uid"] | "";
    const char *type = entry["type"] | "";
    const char *unit = entry["unit"] | "";
    uint8_t version = entry["version"] | (uint8_t)0;
    uint8_t addr = entry["addr"] | (uint8_t)0;

    if (uid[0] == '\0' || addr == 0)
      continue;

    RegisteredModule &m = _registered[_registeredCount++];
    strncpy(m.uid, uid, sizeof(m.uid) - 1);
    m.uid[sizeof(m.uid) - 1] = '\0';
    strncpy(m.type, type, sizeof(m.type) - 1);
    m.type[sizeof(m.type) - 1] = '\0';
    strncpy(m.unit, unit, sizeof(m.unit) - 1);
    m.unit[sizeof(m.unit) - 1] = '\0';
    m.version = version;
    m.addr = addr;
  }

  DEBUG_PRINTF("[MODULE] Loaded %d registered module(s)\n", _registeredCount);
}

void ModuleManager::save()
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (uint8_t i = 0; i < _registeredCount; i++)
  {
    const RegisteredModule &m = _registered[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["uid"] = m.uid;
    obj["type"] = m.type;
    obj["version"] = m.version;
    obj["unit"] = m.unit;
    obj["addr"] = m.addr;
  }

  File f = LittleFS.open(MODULES_FILE, "w");
  if (!f)
  {
    DEBUG_PRINTLN(F("[MODULE] Failed to open modules.json for write"));
    return;
  }
  serializeJson(doc, f);
  f.close();

  DEBUG_PRINTF("[MODULE] Saved %d registered module(s)\n", _registeredCount);

  String output;
  serializeJson(doc, output);
  Logger.Info(LogEvent::MODULE_SAVE, output.c_str());
}

// ============================================================================
// Private: Lookup helpers
// ============================================================================

bool ModuleManager::findRegistered(const char *uid, uint8_t &outIndex) const
{
  for (uint8_t i = 0; i < _registeredCount; i++)
  {
    if (strcmp(_registered[i].uid, uid) == 0)
    {
      outIndex = i;
      return true;
    }
  }
  return false;
}

bool ModuleManager::findDiscovered(const char *uid, uint8_t &outIndex) const
{
  for (uint8_t i = 0; i < _discoveredCount; i++)
  {
    if (strcmp(_discovered[i].desc.uid, uid) == 0)
    {
      outIndex = i;
      return true;
    }
  }
  return false;
}
