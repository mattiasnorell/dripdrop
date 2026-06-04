#include "display.h"
#include "valves.h"
#include <time.h>

DisplayController Display;

static void writeRow(LiquidCrystal_I2C& lcd, uint8_t row, const char* text) {
    lcd.setCursor(0, row);
    char buf[21];
    snprintf(buf, sizeof(buf), "%-20s", text);
    lcd.print(buf);
}

void DisplayController::begin() {
    Wire.begin();
    _lcd.init();
    _lcd.clear();
    _lcd.backlight();
    _backlightOn = true;
    _lastActivity = millis();

    char buf[21];
    snprintf(buf, sizeof(buf), "%s v%s", FIRMWARE_NAME, FIRMWARE_VERSION);
    writeRow(_lcd, 0, buf);

    char bufLoading[21];
    snprintf(bufLoading, sizeof(bufLoading), "%s", "Starting up...");
    writeRow(_lcd, 2, bufLoading);
}

void DisplayController::update(time_t now, bool apMode, const String& ip) {
    unsigned long ms = millis();
    if (ms - _lastUpdate < DISPLAY_UPDATE_INTERVAL_MS) return;
    _lastUpdate = ms;

    renderRow1(ip, apMode);
    renderRow2(now);
    renderRow3();

    if (DISPLAY_BACKLIGHT_TIMEOUT_SECS > 0) {
        bool anyActive = false;
        for (uint8_t i = 0; i < NUM_VALVES; i++) {
            const Valve* v = Valves.getValve(i);
            if (v && v->isOn) { anyActive = true; break; }
        }
        if (anyActive) _lastActivity = ms;

        bool shouldBeOn = (ms - _lastActivity) < (DISPLAY_BACKLIGHT_TIMEOUT_SECS * 1000UL);
        if (shouldBeOn && !_backlightOn)  { _lcd.backlight();   _backlightOn = true; }
        if (!shouldBeOn && _backlightOn)  { _lcd.noBacklight(); _backlightOn = false; }
    }
}

void DisplayController::renderRow1(const String& ip, bool apMode) {
    char buf[21];
    if (apMode)
        snprintf(buf, sizeof(buf), "AP: %s", ip.c_str());
    else
        snprintf(buf, sizeof(buf), "%s", ip.c_str());
    writeRow(_lcd, 1, buf);
}

void DisplayController::renderRow2(time_t now) {
    char buf[21];
    if (now > MIN_VALID_UNIX_TIME) {
        struct tm* t = localtime(&now);
        strftime(buf, sizeof(buf), "%H:%M:%S %y/%m/%d", t);
    } else {
        strncpy(buf, "--:--:-- --/--/--", sizeof(buf));
    }
    writeRow(_lcd, 2, buf);
}

void DisplayController::renderRow3() {
    char v[4];
    for (uint8_t i = 0; i < 4; i++) {
        const Valve* vp = Valves.getValve(i);
        v[i] = (vp && vp->isOn) ? 'X' : '-';
    }
    char buf[21];
    snprintf(buf, sizeof(buf), "Vlv. 1:%c 2:%c 3:%c 4:%c", v[0], v[1], v[2], v[3]);
    _lcd.setCursor(0, 3);
    _lcd.print(buf);
}
