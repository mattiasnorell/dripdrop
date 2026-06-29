/**
 * Minimal LiquidCrystal_I2C stub for native unit testing.
 *
 * Only needs to be constructible so DisplayController (which holds one as a
 * member) compiles; display.cpp itself is not built in native tests.
 */
#pragma once

#include <Arduino.h>

class LiquidCrystal_I2C {
public:
  LiquidCrystal_I2C(uint8_t /*addr*/, uint8_t /*cols*/, uint8_t /*rows*/) {}
  void init() {}
  void clear() {}
  void backlight() {}
  void noBacklight() {}
  void setCursor(uint8_t, uint8_t) {}
  void print(const char*) {}
};
