/**
 * DripDrop - Sensor Manager
 *
 * Reads sensor values from custom I2C sensor boards.
 * Each sensor board has its own I2C address and returns
 * a 2-byte integer value when polled.
 */

#ifndef DRIPDROP_SENSORS_H
#define DRIPDROP_SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// Sentinel value indicating a failed or unavailable sensor read
constexpr int16_t SENSOR_READ_ERROR = INT16_MIN;

class SensorManager {
public:
  /**
   * Initialize I2C bus - call once in setup()
   */
  void begin();

  /**
   * Read a sensor value by string ID.
   * @param sensorId One of: "temp1", "hum1", "soil1", "water1"
   * @return Sensor value as int16_t, or SENSOR_READ_ERROR on failure
   */
  int16_t read(const char* sensorId);

private:
  /**
   * Poll a sensor board over I2C.
   * @param addr I2C address of the sensor board
   * @return 2-byte value, or SENSOR_READ_ERROR if board doesn't respond
   */
  int16_t pollI2C(uint8_t addr);

  /**
   * Resolve sensor ID string to I2C address.
   * @return I2C address, or 0 if unknown/unconnected
   */
  uint8_t resolveAddress(const char* sensorId);
};

extern SensorManager Sensors;

#endif // DRIPDROP_SENSORS_H
