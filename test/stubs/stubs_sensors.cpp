/**
 * SensorManager stub for native unit testing.
 */
#include "../../src/sensors.h"

SensorManager Sensors;

static int16_t stubSensorReadings[4] = {
  SENSOR_READ_ERROR, SENSOR_READ_ERROR, SENSOR_READ_ERROR, SENSOR_READ_ERROR
};

// Test helper: set what Sensors.read() returns for a given sensorId
void stub_setSensorReading(const char* sensorId, int16_t value) {
  if (strcmp(sensorId, "temp1") == 0)  stubSensorReadings[0] = value;
  if (strcmp(sensorId, "hum1") == 0)   stubSensorReadings[1] = value;
  if (strcmp(sensorId, "soil1") == 0)  stubSensorReadings[2] = value;
  if (strcmp(sensorId, "water1") == 0) stubSensorReadings[3] = value;
}

void stub_resetSensorReadings() {
  for (int i = 0; i < 4; i++) stubSensorReadings[i] = SENSOR_READ_ERROR;
}

void SensorManager::begin() {}

int16_t SensorManager::read(const char* sensorId) {
  if (strcmp(sensorId, "temp1") == 0)  return stubSensorReadings[0];
  if (strcmp(sensorId, "hum1") == 0)   return stubSensorReadings[1];
  if (strcmp(sensorId, "soil1") == 0)  return stubSensorReadings[2];
  if (strcmp(sensorId, "water1") == 0) return stubSensorReadings[3];
  return SENSOR_READ_ERROR;
}

int16_t SensorManager::pollI2C(uint8_t) { return SENSOR_READ_ERROR; }
uint8_t SensorManager::resolveAddress(const char*) { return 0; }
