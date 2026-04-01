/**
 * DripDrop - Schedule Manager Implementation
 */

#include "scheduler.h"
#include "valves.h"

// Global instance
SchedulerClass Scheduler;

void SchedulerClass::begin() {
  DEBUG_SCHEDULE("Initializing scheduler...\n");
  
  // Initialize all schedule slots
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    _schedules[i].clear();
  }
  
  // Load from EEPROM
  load();
}

void SchedulerClass::check(time_t currentTime) {
  if (currentTime < MIN_VALID_UNIX_TIME) {
    // Time not yet synchronized
    return;
  }
  
  struct tm timeInfo;
  localtime_r(&currentTime, &timeInfo);  // Thread-safe version
  
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (_schedules[i].isEmpty() || !_schedules[i].isValid()) {
      continue;
    }
    
    int8_t valveIndex = Valves.findByValveId(_schedules[i].valveId);
    if (valveIndex < 0) continue;
    
    Valve* valve = Valves.getValve(valveIndex);
    if (!valve) continue;
    
    // Skip if valve is manually controlled
    if (valve->isManuallyControlled()) {
      continue;
    }
    
    // Skip if valve is timer controlled (timer has priority over schedule)
    if (valve->isTimerControlled()) {
      continue;
    }
    
    bool shouldBeOn = isScheduleActive(_schedules[i], &timeInfo);
    
    if (shouldBeOn && !valve->isOn) {
      Valves.setState(valveIndex, true, ValveSource::SCHEDULE);
      DEBUG_SCHEDULE("Schedule %d activated valve %d\n", i, _schedules[i].valveId);
    } else if (!shouldBeOn && valve->isScheduleControlled()) {
      Valves.setState(valveIndex, false, ValveSource::NONE);
      DEBUG_SCHEDULE("Schedule %d deactivated valve %d\n", i, _schedules[i].valveId);
    }
  }
}

bool SchedulerClass::isScheduleActive(const Schedule& schedule, const struct tm* timeInfo) const {
  // days bitmask: bit 7=Sun(0), 6=Mon(1), 5=Tue(2), 4=Wed(3), 3=Thu(4), 2=Fri(5), 1=Sat(6)
  uint32_t currentSeconds = timeInfo->tm_hour * 3600 + timeInfo->tm_min * 60 + timeInfo->tm_sec;
  uint32_t scheduleStart  = schedule.fromHour * 3600 + schedule.fromMinute * 60;
  uint32_t scheduleEnd    = scheduleStart + schedule.duration;

  if (scheduleEnd > 86400) {
    // Pre-midnight portion: still on the day the schedule is defined for
    if (currentSeconds >= scheduleStart) {
      uint8_t dayBit = 7 - timeInfo->tm_wday;
      return (schedule.days & (1 << dayBit)) != 0;
    }
    // Post-midnight portion: schedule was started yesterday
    if (currentSeconds < (scheduleEnd - 86400)) {
      uint8_t prevWday = (timeInfo->tm_wday + 6) % 7;
      uint8_t dayBit   = 7 - prevWday;
      return (schedule.days & (1 << dayBit)) != 0;
    }
    return false;
  }

  // Non-wrapping schedule: check current day then time window
  uint8_t dayBit = 7 - timeInfo->tm_wday;
  if (!(schedule.days & (1 << dayBit))) {
    return false;
  }
  return currentSeconds >= scheduleStart && currentSeconds < scheduleEnd;
}

int8_t SchedulerClass::add(uint8_t valveId, uint8_t fromHour, uint8_t fromMinute, 
                           uint16_t duration, uint8_t days) {
  // Validate valve
  if (!Valves.isValidId(valveId)) {
    DEBUG_SCHEDULE("Add failed: invalid valveId %d\n", valveId);
    return -1;
  }
  
  // Validate parameters
  if (!isValid(fromHour, fromMinute, duration)) {
    DEBUG_SCHEDULE("Add failed: invalid parameters\n");
    return -1;
  }
  
  // Find empty slot
  int8_t slot = findEmptySlot();
  if (slot < 0) {
    DEBUG_SCHEDULE("Add failed: no empty slots\n");
    return -1;
  }
  
  // Create schedule
  _schedules[slot].valveId = valveId;
  _schedules[slot].fromHour = fromHour;
  _schedules[slot].fromMinute = fromMinute;
  _schedules[slot].duration = duration;
  _schedules[slot].days = days;
  
  _dirty = true;
  save();
  
  DEBUG_SCHEDULE("Added schedule %d: valve=%d, time=%02d:%02d, duration=%ds, days=0x%02X\n",
                 slot, valveId, fromHour, fromMinute, duration, days);
  
  return slot;
}

bool SchedulerClass::update(uint8_t scheduleId, uint8_t valveId, uint8_t fromHour, 
                            uint8_t fromMinute, uint16_t duration, uint8_t days) {
  if (scheduleId >= MAX_SCHEDULES) {
    return false;
  }
  
  if (!Valves.isValidId(valveId)) {
    return false;
  }
  
  if (!isValid(fromHour, fromMinute, duration)) {
    return false;
  }
  
  _schedules[scheduleId].valveId = valveId;
  _schedules[scheduleId].fromHour = fromHour;
  _schedules[scheduleId].fromMinute = fromMinute;
  _schedules[scheduleId].duration = duration;
  _schedules[scheduleId].days = days;
  
  _dirty = true;
  save();
  
  DEBUG_SCHEDULE("Updated schedule %d\n", scheduleId);
  
  return true;
}

bool SchedulerClass::remove(uint8_t scheduleId) {
  if (scheduleId >= MAX_SCHEDULES) {
    return false;
  }
  
  _schedules[scheduleId].clear();
  _dirty = true;
  save();
  
  DEBUG_SCHEDULE("Removed schedule %d\n", scheduleId);
  
  return true;
}

void SchedulerClass::removeAll() {
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    _schedules[i].clear();
  }
  
  _dirty = true;
  save();
  
  DEBUG_SCHEDULE("Removed all schedules\n");
}

Schedule* SchedulerClass::get(uint8_t index) {
  if (index >= MAX_SCHEDULES) return nullptr;
  return &_schedules[index];
}

const Schedule* SchedulerClass::get(uint8_t index) const {
  if (index >= MAX_SCHEDULES) return nullptr;
  return &_schedules[index];
}

uint8_t SchedulerClass::getActiveCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (!_schedules[i].isEmpty()) {
      count++;
    }
  }
  return count;
}

int8_t SchedulerClass::findEmptySlot() const {
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (_schedules[i].isEmpty()) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

bool SchedulerClass::isValid(uint8_t fromHour, uint8_t fromMinute, uint16_t duration) {
  if (fromHour >= 24) return false;
  if (fromMinute >= 60) return false;
  if (duration == 0 || duration > MAX_SCHEDULE_DURATION_SEC) return false;
  return true;
}

void SchedulerClass::save() {
  if (!_dirty) return;
  
  // Write header
  EepromHeader header;
  header.magic = EepromConfig::MAGIC_NUMBER;
  header.version = EepromConfig::DATA_VERSION;
  header.scheduleCount = getActiveCount();
  header.checksum = calculateChecksum();
  
  EEPROM.put(EepromConfig::HEADER_ADDR, header);
  EEPROM.put(SCHEDULE_EEPROM_ADDR, _schedules);
  EEPROM.commit();
  
  _dirty = false;
  
  DEBUG_SCHEDULE("Saved %d schedules to EEPROM (checksum: 0x%04X)\n", 
                 header.scheduleCount, header.checksum);
}

void SchedulerClass::load() {
  EepromHeader header;
  EEPROM.get(EepromConfig::HEADER_ADDR, header);
  
  // Check magic number
  if (header.magic != EepromConfig::MAGIC_NUMBER) {
    DEBUG_SCHEDULE("EEPROM not initialized (magic: 0x%08X), initializing...\n", header.magic);
    initializeEeprom();
    return;
  }
  
  // Check version - could add migration logic here
  if (header.version != EepromConfig::DATA_VERSION) {
    DEBUG_SCHEDULE("EEPROM version mismatch (got %d, expected %d), reinitializing...\n",
                   header.version, EepromConfig::DATA_VERSION);
    initializeEeprom();
    return;
  }
  
  // Load schedules
  EEPROM.get(SCHEDULE_EEPROM_ADDR, _schedules);
  
  // Verify checksum
  uint16_t calculatedChecksum = calculateChecksum();
  if (header.checksum != calculatedChecksum) {
    DEBUG_SCHEDULE("EEPROM checksum mismatch (got 0x%04X, expected 0x%04X), reinitializing...\n",
                   header.checksum, calculatedChecksum);
    initializeEeprom();
    return;
  }
  
  // Validate loaded data and sanitize any invalid entries
  uint8_t validCount = 0;
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (!_schedules[i].isEmpty()) {
      if (_schedules[i].isValid() && Valves.isValidId(_schedules[i].valveId)) {
        validCount++;
      } else {
        // Invalid data - clear this slot
        DEBUG_SCHEDULE("Schedule %d invalid, clearing\n", i);
        _schedules[i].clear();
        _dirty = true;
      }
    }
  }
  
  if (_dirty) {
    save();  // Save corrected data
  }
  
  DEBUG_SCHEDULE("Loaded %d valid schedules from EEPROM\n", validCount);
}

void SchedulerClass::initializeEeprom() {
  DEBUG_SCHEDULE("Initializing EEPROM with defaults...\n");
  
  // Clear all schedules
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    _schedules[i].clear();
  }
  
  _dirty = true;
  save();
}

uint16_t SchedulerClass::calculateChecksum() const {
  uint16_t checksum = 0;
  const uint8_t* data = reinterpret_cast<const uint8_t*>(_schedules);
  size_t len = sizeof(_schedules);
  
  for (size_t i = 0; i < len; i++) {
    checksum += data[i];
    checksum = (checksum << 1) | (checksum >> 15);  // Rotate left
  }
  
  return checksum;
}
