/**
 * DripDrop - Event Logger
 *
 * Best-effort outbound HTTP POST logging. URL and token are configured at
 * runtime via the /system/logs endpoint and persisted in LittleFS.
 */

#ifndef DRIPDROP_LOGGER_H
#define DRIPDROP_LOGGER_H

#include <Arduino.h>
#include "config.h"

namespace LogEvent
{
  constexpr const char *VALVE_ON = "valve.on";
  constexpr const char *VALVE_OFF = "valve.off";
  constexpr const char *VALVES_ALL_OFF = "valves.all_off";
  constexpr const char *TIMER_START = "timer.start";
  constexpr const char *TIMER_ABORT = "timer.abort";
  constexpr const char *TIMER_EXPIRE = "timer.expire";
  constexpr const char *SCENARIO_ADD = "scenario.add";
  constexpr const char *SCENARIO_UPDATE = "scenario.update";
  constexpr const char *SCENARIO_DELETE = "scenario.delete";
  constexpr const char *SCENARIO_FIRE = "scenario.fire";
  constexpr const char *SYSTEM_NTP_SYNCED = "system.ntp_synced";
  constexpr const char *SYSTEM_BOOT = "system.boot";
  constexpr const char *SYSTEM_REBOOT = "system.reboot";
  constexpr const char *SYSTEM_WIFI_FAILED = "system.wifi_failed";
}

class EventLogger
{
public:
  EventLogger() : _device(EVENT_LOG_DEVICE_ID) {}

  void setUrl(const char *url) { _url = url; }
  void setToken(const char *token) { _token = token; }
  void setDevice(const char *device) { _device = (device && strlen(device) > 0) ? device : EVENT_LOG_DEVICE_ID; }
  const String &getUrl() const { return _url; }
  const String &getToken() const { return _token; }
  const String &getDevice() const { return _device; }

  // Best-effort send. Fails silently if WiFi is down or URL is empty.
  // details: optional JSON object string e.g. "{\"valveId\":1}" or nullptr
  void Info(const char *event, const char *details = nullptr) { log("info", event, details); }
  void Warning(const char *event, const char *details = nullptr) { log("warning", event, details); }
  void Error(const char *event, const char *details = nullptr) { log("err", event, details); }

private:
  void log(const char *level, const char *event, const char *details);
  String _url;
  String _token;
  String _device;
};

extern EventLogger Logger;

#endif // DRIPDROP_LOGGER_H
