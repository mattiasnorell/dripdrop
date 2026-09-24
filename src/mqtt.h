/**
 * DripDrop - MQTT Manager
 *
 * Publishes relay states, system status, and sensor readings to an MQTT broker.
 * Subscribes to command topics for remote relay and timer control.
 * Uses LWT for availability tracking (online/offline).
 *
 * Disabled by default at runtime (_enabled = false).
 * Enable via setEnabled(true) or the GUI (POST /system/mqtt).
 */

#ifndef DRIPDROP_MQTT_H
#define DRIPDROP_MQTT_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

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
  constexpr const char *RELAY_ON = "relay.on";
  constexpr const char *RELAY_OFF = "relay.off";
  constexpr const char *RELAYS_ALL_OFF = "relays.all_off";
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

  void publishRelayState(uint8_t relayId);
  void publishAllRelayStates();
  void publishSystemStatus();
  void publishSensorReading(const char* uid, float value, const char* type, const char* unit);
  void publishAllSensorReadings();
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
  WiFiClient _wifiClient;
  mutable PubSubClient _mqttClient;

  void reconnect(unsigned long now);
  void onMessage(char* topic, byte* payload, unsigned int length);
  void subscribe();
  void publishAvailability();
  void buildTopicPrefix();

  unsigned long _lastReconnectAttempt = 0;
  unsigned long _lastStatePublish = 0;

  bool _enabled = false;
  String _server;
  uint16_t _port = MQTT_PORT;
  String _user;
  String _password;
  String _topicPrefix;
};

extern MqttManager Mqtt;

#endif // DRIPDROP_MQTT_H
