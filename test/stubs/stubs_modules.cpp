/**
 * ModuleManager stub for native unit testing.
 *
 * Include this file in test suites that need to compile against modules.h
 * but are NOT testing module behaviour themselves (e.g. test_scenarios).
 */
#include "../../src/modules.h"

ModuleManager Modules;

static struct StubEntry {
  char  uid[13];
  float value;
  bool  set;
} _stubEntries[8] = {};

void stub_setModuleReading(const char* uid, float value) {
  for (auto& e : _stubEntries) {
    if (!e.set || strcmp(e.uid, uid) == 0) {
      strncpy(e.uid, uid, sizeof(e.uid) - 1);
      e.uid[sizeof(e.uid) - 1] = '\0';
      e.value = value;
      e.set   = true;
      return;
    }
  }
}

void stub_resetModuleReadings() {
  for (auto& e : _stubEntries) e = {};
}

void    ModuleManager::begin()                              {}
uint8_t ModuleManager::scanModules()                        { return 0; }
bool    ModuleManager::registerModule(const char*)          { return false; }
bool    ModuleManager::removeModule(const char*)            { return false; }
bool    ModuleManager::setCustomName(const char*, const char*) { return true; }
bool    ModuleManager::setUnit(const char*, const char*)       { return true; }
bool    ModuleManager::commandModule(uint8_t, uint8_t)         { return false; }

uint8_t ModuleManager::addrForUid(const char* uid) const {
  for (uint8_t i = 0; i < 8; i++) {
    if (_stubEntries[i].set && strcmp(_stubEntries[i].uid, uid) == 0)
      return i + 1;
  }
  return 0;
}

bool ModuleManager::readModule(uint8_t addr, SensorResponse& out) {
  uint8_t i = addr - 1;
  if (i >= 8 || !_stubEntries[i].set) return false;
  out.status = 0x00;
  out.value  = _stubEntries[i].value;
  return true;
}

void ModuleManager::serializeScan(String& out) const       { out = "[]"; }
void ModuleManager::serializeRegistered(String& out) const { out = "[]"; }

bool ModuleManager::scanAddress(uint8_t, Descriptor&)              { return false; }
void ModuleManager::load()                                          {}
void ModuleManager::save()                                          {}
bool ModuleManager::findRegistered(const char*, uint8_t&) const    { return false; }
bool ModuleManager::findRegisteredByAddr(uint8_t, uint8_t&) const  { return false; }
bool ModuleManager::findDiscovered(const char*, uint8_t&) const    { return false; }
