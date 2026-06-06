#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

class DisplayController {
public:
    void begin();
    void update(time_t now, bool apMode, const String& ip);
    void showOverride(const String& r0, const String& r1, const String& r2, const String& r3, uint32_t timeoutSecs);

private:
    LiquidCrystal_I2C _lcd{LCD_I2C_ADDR, 20, 4};
    unsigned long _lastUpdate = 0;
    unsigned long _lastActivity = 0;
    bool _backlightOn = true;
    unsigned long _overrideEndMs = 0;
    char _overrideRows[4][21];

    void renderRow0();
    void renderRow1(const String& ip, bool apMode);
    void renderRow2(time_t now);
    void renderRow3();
};

extern DisplayController Display;
