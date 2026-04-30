/**
 * DripDrop - MQTT Manager
 *
 * Publishes valve states, system status, and sensor readings to an MQTT broker.
 * Subscribes to command topics for remote valve and timer control.
 * Uses LWT for availability tracking (online/offline).
 *
 * Disabled at compile time by default (MQTT_ENABLED = false).
 * All public methods are safe to call when disabled — they become no-ops.
 */

#ifndef DRIPDROP_MQTT_H
#define DRIPDROP_MQTT_H

#include <Arduino.h>
#include "config.h"

#if MQTT_ENABLED
  #include <WiFi.h>
  #include <PubSubClient.h>
#endif

// Log severity level constants
namespace LogLevel
{
  constexpr const char *DEBUG   = "debug";
  constexpr const char *INFO    = "info";
  constexpr const char *WARNING = "warning";
  constexpr const char *ERR     = "err";
}

// Event name constants for MQTT logging
namespace LogEvent
{
  constexpr const char *MODULE_SAVE = "module.save";
  constexpr const char *MODULE_REMOVED = "module.removed";
  constexpr const char *MODULE_REGISTRATION = "module.registration";
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

class MqttManager {
public:
  void begin();
  void loop(unsigned long now);

  void publishValveState(uint8_t valveId);
  void publishAllValveStates();
  void publishSystemStatus();
  void publishSensorReading(const char* uid, float value);
  void publishEvent(const char* level, const char* event, const char* details = nullptr);

  bool isConnected() const;

  void setServer(const char* server, uint16_t port);
  void setCredentials(const char* user, const char* password);
  const String& getServer() const   { return _server; }
  uint16_t getPort() const          { return _port; }
  const String& getUser() const     { return _user; }

  void setEnabled(bool enabled);
  bool getEnabled() const            { return _enabled; }

  void disconnect();

private:
#if MQTT_ENABLED
  WiFiClient _wifiClient;
  PubSubClient _mqttClient;

  void reconnect(unsigned long now);
  void onMessage(char* topic, byte* payload, unsigned int length);
  void subscribe();
  void publishAvailability();
  void buildTopicPrefix();

  unsigned long _lastReconnectAttempt = 0;
  unsigned long _lastStatePublish = 0;
#endif

  bool _enabled = MQTT_ENABLED;
  String _server;
  uint16_t _port = MQTT_PORT;
  String _user;
  String _password;
  String _topicPrefix;
};

extern MqttManager Mqtt;

#endif // DRIPDROP_MQTT_H
