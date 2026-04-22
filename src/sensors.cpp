/**
 * DripDrop - Sensor Manager Implementation
 */

#include "sensors.h"

SensorManager Sensors;

void SensorManager::begin() {
  Wire.begin();
  Wire.setTimeout(100);  // 100ms cap — prevents watchdog resets from absent sensors
  DEBUG_PRINTLN(F("I2C sensor bus initialized"));
}

int16_t SensorManager::read(const char* sensorId) {
  uint8_t addr = resolveAddress(sensorId);
  if (addr == 0) {
    return SENSOR_READ_ERROR;
  }
  return pollI2C(addr);
}

int16_t SensorManager::pollI2C(uint8_t addr) {
  uint8_t bytesReceived = Wire.requestFrom(addr, (uint8_t)2);
  if (bytesReceived != 2) {
    DEBUG_PRINTF("[SENSOR] I2C read failed from 0x%02X (got %d bytes)\n", addr, bytesReceived);
    return SENSOR_READ_ERROR;
  }

  uint8_t high = Wire.read();
  uint8_t low = Wire.read();
  return (int16_t)((high << 8) | low);
}

uint8_t SensorManager::resolveAddress(const char* sensorId) {
  if (strcmp(sensorId, "temp1") == 0)  return SENSOR_ADDR_TEMP1;
  if (strcmp(sensorId, "hum1") == 0)   return SENSOR_ADDR_HUM1;
  if (strcmp(sensorId, "soil1") == 0)  return SENSOR_ADDR_SOIL1;
  if (strcmp(sensorId, "water1") == 0) return SENSOR_ADDR_WATER1;
  return 0;
}
