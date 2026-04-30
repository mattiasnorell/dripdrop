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

class MqttManager {
public:
  void begin();
  void loop(unsigned long now);

  void publishValveState(uint8_t valveId);
  void publishAllValveStates();
  void publishSystemStatus();
  void publishSensorReading(const char* uid, float value);

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
