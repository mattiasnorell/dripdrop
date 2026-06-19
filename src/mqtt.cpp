/**
 * DripDrop - MQTT Manager Implementation
 */

#include "mqtt.h"
#include <ArduinoJson.h>
#include "valves.h"
#include "timers.h"
#include "types.h"

extern String deviceName;

MqttManager Mqtt;

// Helper: convert ValveSource enum to string
static const char* sourceToString(ValveSource src) {
  switch (src) {
    case ValveSource::MANUAL:   return "MANUAL";
    case ValveSource::TIMER:    return "TIMER";
    case ValveSource::SCENARIO: return "SCENARIO";
    default:                    return "NONE";
  }
}

void MqttManager::begin() {
  if (!_enabled) {
    DEBUG_PRINTLN(F("[MQTT] Disabled"));
    return;
  }

  if (_server.length() == 0) {
    _server = MQTT_SERVER;
    _port = MQTT_PORT;
    _user = MQTT_USER;
    _password = MQTT_PASSWORD;
  }

  if (_server.length() == 0) {
    DEBUG_PRINTLN(F("[MQTT] No server configured"));
    return;
  }

  _mqttClient.setClient(_wifiClient);
  _mqttClient.setServer(_server.c_str(), _port);
  _mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  _mqttClient.setSocketTimeout(2);
  _mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
    Mqtt.onMessage(topic, payload, length);
  });

  buildTopicPrefix();

  DEBUG_PRINTF("[MQTT] Configured: %s:%d\n", _server.c_str(), _port);
}

void MqttManager::loop(unsigned long now) {
  if (!_enabled) return;
  if (_server.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (!_mqttClient.connected()) {
    reconnect(now);
  }

  if (_mqttClient.connected()) {
    _mqttClient.loop();

    if (now - _lastStatePublish >= MQTT_STATE_INTERVAL_MS) {
      _lastStatePublish = now;
      publishAllValveStates();
      publishSystemStatus();
    }
  }
}

void MqttManager::reconnect(unsigned long now) {
  if (now - _lastReconnectAttempt < MQTT_RECONNECT_INTERVAL_MS) return;
  _lastReconnectAttempt = now;

  String clientId = "dripdrop-" + deviceName;
  String willTopic = _topicPrefix + "/status";

  DEBUG_PRINTF("[MQTT] Connecting to %s:%d...\n", _server.c_str(), _port);

  bool connected = _mqttClient.connect(
    clientId.c_str(),
    _user.length() ? _user.c_str() : nullptr,
    _password.length() ? _password.c_str() : nullptr,
    willTopic.c_str(),
    1,      // QoS 1
    true,   // retain
    "offline"
  );

  if (connected) {
    DEBUG_PRINTLN(F("[MQTT] Connected"));
    publishAvailability();
    subscribe();
    publishAllValveStates();
    publishSystemStatus();
    _lastStatePublish = now;
  } else {
    DEBUG_PRINTF("[MQTT] Connect failed, rc=%d\n", _mqttClient.state());
  }
}

void MqttManager::subscribe() {
  String valveTopic = _topicPrefix + "/valve/+/set";
  String timerTopic = _topicPrefix + "/timer/+/set";

  _mqttClient.subscribe(valveTopic.c_str());
  _mqttClient.subscribe(timerTopic.c_str());

  DEBUG_PRINTF("[MQTT] Subscribed to %s\n", valveTopic.c_str());
  DEBUG_PRINTF("[MQTT] Subscribed to %s\n", timerTopic.c_str());
}

void MqttManager::publishAvailability() {
  String topic = _topicPrefix + "/status";
  _mqttClient.publish(topic.c_str(), "online", true);
}

void MqttManager::onMessage(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  String t(topic);
  String prefix = _topicPrefix + "/";

  if (!t.startsWith(prefix)) return;
  String relative = t.substring(prefix.length());

  // Parse: valve/{id}/set
  if (relative.startsWith("valve/") && relative.endsWith("/set")) {
    String idStr = relative.substring(6, relative.length() - 4);
    uint8_t valveId = idStr.toInt();
    int8_t index = Valves.findByValveId(valveId);
    if (index < 0) return;

    if (strcasecmp(msg, "ON") == 0) {
      Valves.setState(index, true, ValveSource::MANUAL);
    } else if (strcasecmp(msg, "OFF") == 0) {
      Timers.abort(valveId);
      Valves.setState(index, false, ValveSource::NONE);
    }
    publishValveState(valveId);
    return;
  }

  // Parse: timer/{id}/set
  if (relative.startsWith("timer/") && relative.endsWith("/set")) {
    String idStr = relative.substring(6, relative.length() - 4);
    uint8_t valveId = idStr.toInt();
    if (!Valves.isValidId(valveId)) return;

    if (strcasecmp(msg, "ABORT") == 0) {
      Timers.abort(valveId);
    } else {
      JsonDocument doc;
      if (deserializeJson(doc, msg) == DeserializationError::Ok && doc["duration"].is<int>()) {
        uint32_t duration = doc["duration"];
        if (duration > 0 && duration <= MAX_TIMER_DURATION_SEC) {
          Timers.start(valveId, duration);
        }
      }
    }
    publishValveState(valveId);
    return;
  }
}

void MqttManager::publishValveState(uint8_t valveId) {
  if (!_mqttClient.connected()) return;

  int8_t index = Valves.findByValveId(valveId);
  if (index < 0) return;

  const Valve* valve = Valves.getValve(index);
  if (!valve) return;

  // Build the payload on the stack to avoid per-call heap allocation.
  char payload[128];
  int n = snprintf(payload, sizeof(payload),
                   "{\"isOn\":%s,\"source\":\"%s\"",
                   valve->isOn ? "true" : "false",
                   sourceToString(valve->source));
  if (valve->lastRunStart > 0 && n < (int)sizeof(payload)) {
    n += snprintf(payload + n, sizeof(payload) - n,
                  ",\"lastRunStart\":%ld", (long)valve->lastRunStart);
  }
  time_t now = time(nullptr);
  if (Timers.isActive(valveId, now) && n < (int)sizeof(payload)) {
    n += snprintf(payload + n, sizeof(payload) - n,
                  ",\"timerRemaining\":%lu",
                  (unsigned long)Timers.getRemainingSeconds(valveId, now));
  }
  if (n < (int)sizeof(payload)) {
    snprintf(payload + n, sizeof(payload) - n, "}");
  }

  char topic[64];
  snprintf(topic, sizeof(topic), "%s/valve/%d/state", _topicPrefix.c_str(), valveId);
  _mqttClient.publish(topic, payload, true);
}

void MqttManager::publishAllValveStates() {
  for (uint8_t i = 1; i <= NUM_VALVES; i++) {
    publishValveState(i);
  }
}

void MqttManager::publishSystemStatus() {
  if (!_mqttClient.connected()) return;

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"uptime\":%lu,\"freeHeap\":%lu,\"wifi\":%d,\"ntp\":%s,\"valves\":%u}",
           (unsigned long)millis(),
           (unsigned long)ESP.getFreeHeap(),
           (int)WiFi.RSSI(),
           (time(nullptr) > MIN_VALID_UNIX_TIME) ? "true" : "false",
           (unsigned)Valves.getActiveCount());

  char topic[64];
  snprintf(topic, sizeof(topic), "%s/system/state", _topicPrefix.c_str());
  _mqttClient.publish(topic, payload, true);
}

void MqttManager::publishSensorReading(const char* uid, float value) {
  if (!_mqttClient.connected()) return;

  char payload[32];
  snprintf(payload, sizeof(payload), "{\"value\":%.2f}", value);

  char topic[96];
  snprintf(topic, sizeof(topic), "%s/sensor/%s/state", _topicPrefix.c_str(), uid);
  _mqttClient.publish(topic, payload, true);
}

bool MqttManager::isConnected() const {
  return _mqttClient.connected();
}

void MqttManager::setServer(const char* server, uint16_t port) {
  _server = server;
  _port = port;
}

void MqttManager::setCredentials(const char* user, const char* password) {
  _user = user;
  _password = password;
}

void MqttManager::setEnabled(bool enabled) {
  _enabled = enabled;
  if (!enabled) disconnect();
}

void MqttManager::disconnect() {
  if (_mqttClient.connected()) {
    String topic = _topicPrefix + "/status";
    _mqttClient.publish(topic.c_str(), "offline", true);
    _mqttClient.disconnect();
  }
}

void MqttManager::publishEvent(const char* level, const char* event, const char* details) {
  if (!_mqttClient.connected()) return;

  // `details` is already a JSON object string built by the caller, so splice it
  // in directly instead of parsing and re-serializing it through ArduinoJson.
  char payload[MQTT_BUFFER_SIZE];
  int n = snprintf(payload, sizeof(payload),
                   "{\"app\":\"dripdrop\",\"module\":\"%s\",\"level\":\"%s\",\"event\":\"%s\"",
                   deviceName.c_str(), level, event);
  if (details && details[0] != '\0' && n < (int)sizeof(payload)) {
    n += snprintf(payload + n, sizeof(payload) - n, ",\"details\":%s", details);
  }
  if (n < (int)sizeof(payload)) {
    snprintf(payload + n, sizeof(payload) - n, "}");
  }

  char topic[64];
  snprintf(topic, sizeof(topic), "%s/event", _topicPrefix.c_str());
  _mqttClient.publish(topic, payload);
}

void MqttManager::buildTopicPrefix() {
  _topicPrefix = "dripdrop/" + deviceName;
}
