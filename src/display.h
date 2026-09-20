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

    // Write four lines to the LCD immediately (synchronous I2C), bypassing the
    // loop()-driven refresh. Used for blocking operations like the self-update, during
    // which loop()/update() is not running so showOverride() would never render.
    void showMessage(const String& r0, const String& r1, const String& r2, const String& r3);

private:
    LiquidCrystal_I2C _lcd{LCD_I2C_ADDR, 20, 4};
    unsigned long _lastActivity = 0;
    bool _backlightOn = true;
    unsigned long _overrideStartMs = 0;
    unsigned long _overrideTimeoutMs = 0;
    char _overrideRows[4][21];

    // Last text written to each row; lets update() skip slow I2C writes when
    // the content has not changed since the previous refresh.
    char _rowCache[4][21] = {};

    // Write a (space-padded) line to the LCD only if it differs from the cache.
    void writeRow(uint8_t row, const char* text);

    void renderRow0();
    void renderRow1(const String& ip, bool apMode);
    void renderRow2(time_t now);
    void renderRow3();
};

extern DisplayController Display;
