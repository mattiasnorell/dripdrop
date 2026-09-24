/**
 * DripDrop - MQTT Manager Implementation
 */

#include "mqtt.h"
#include <ArduinoJson.h>
#include "relays.h"
#include "timers.h"
#include "types.h"
#include "modules.h"

extern String deviceName;

MqttManager Mqtt;

// Helper: convert RelaySource enum to string
static const char* sourceToString(RelaySource src) {
  switch (src) {
    case RelaySource::MANUAL:   return "MANUAL";
    case RelaySource::TIMER:    return "TIMER";
    case RelaySource::SCENARIO: return "SCENARIO";
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
      publishAllRelayStates();
      publishSystemStatus();
      publishAllSensorReadings();
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
    publishAllRelayStates();
    publishSystemStatus();
    _lastStatePublish = now;
  } else {
    DEBUG_PRINTF("[MQTT] Connect failed, rc=%d\n", _mqttClient.state());
  }
}

void MqttManager::subscribe() {
  String relayTopic = _topicPrefix + "/relay/+/set";
  String timerTopic = _topicPrefix + "/timer/+/set";

  _mqttClient.subscribe(relayTopic.c_str());
  _mqttClient.subscribe(timerTopic.c_str());

  DEBUG_PRINTF("[MQTT] Subscribed to %s\n", relayTopic.c_str());
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

  // Parse: relay/{id}/set
  if (relative.startsWith("relay/") && relative.endsWith("/set")) {
    String idStr = relative.substring(6, relative.length() - 4);
    uint8_t relayId = idStr.toInt();
    int8_t index = Relays.findByRelayId(relayId);
    if (index < 0) return;

    if (strcasecmp(msg, "ON") == 0) {
      Relays.setState(index, true, RelaySource::MANUAL);
    } else if (strcasecmp(msg, "OFF") == 0) {
      Timers.abort(relayId);
      Relays.setState(index, false, RelaySource::NONE);
    }
    publishRelayState(relayId);
    return;
  }

  // Parse: timer/{id}/set
  if (relative.startsWith("timer/") && relative.endsWith("/set")) {
    String idStr = relative.substring(6, relative.length() - 4);
    uint8_t relayId = idStr.toInt();
    if (!Relays.isValidId(relayId)) return;

    if (strcasecmp(msg, "ABORT") == 0) {
      Timers.abort(relayId);
    } else {
      JsonDocument doc;
      if (deserializeJson(doc, msg) == DeserializationError::Ok && doc["duration"].is<int>()) {
        uint32_t duration = doc["duration"];
        if (duration > 0 && duration <= MAX_TIMER_DURATION_SEC) {
          Timers.start(relayId, duration);
        }
      }
    }
    publishRelayState(relayId);
    return;
  }
}

void MqttManager::publishRelayState(uint8_t relayId) {
  if (!_mqttClient.connected()) return;

  int8_t index = Relays.findByRelayId(relayId);
  if (index < 0) return;

  const Relay* relay = Relays.getRelay(index);
  if (!relay) return;

  // Build the payload on the stack to avoid per-call heap allocation.
  char payload[128];
  int n = snprintf(payload, sizeof(payload),
                   "{\"isOn\":%s,\"source\":\"%s\"",
                   relay->isOn ? "true" : "false",
                   sourceToString(relay->source));
  if (relay->lastRunStart > 0 && n < (int)sizeof(payload)) {
    n += snprintf(payload + n, sizeof(payload) - n,
                  ",\"lastRunStart\":%ld", (long)relay->lastRunStart);
  }
  time_t now = time(nullptr);
  if (Timers.isActive(relayId, now) && n < (int)sizeof(payload)) {
    n += snprintf(payload + n, sizeof(payload) - n,
                  ",\"timerRemaining\":%lu",
                  (unsigned long)Timers.getRemainingSeconds(relayId, now));
  }
  if (n < (int)sizeof(payload)) {
    snprintf(payload + n, sizeof(payload) - n, "}");
  }

  char topic[64];
  snprintf(topic, sizeof(topic), "%s/relay/%d/state", _topicPrefix.c_str(), relayId);
  _mqttClient.publish(topic, payload, true);
}

void MqttManager::publishAllRelayStates() {
  for (uint8_t i = 1; i <= NUM_RELAYS; i++) {
    publishRelayState(i);
  }
}

void MqttManager::publishSystemStatus() {
  if (!_mqttClient.connected()) return;

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"uptime\":%lu,\"freeHeap\":%lu,\"wifi\":%d,\"ntp\":%s,\"relays\":%u}",
           (unsigned long)millis(),
           (unsigned long)ESP.getFreeHeap(),
           (int)WiFi.RSSI(),
           (time(nullptr) > MIN_VALID_UNIX_TIME) ? "true" : "false",
           (unsigned)Relays.getActiveCount());

  char topic[64];
  snprintf(topic, sizeof(topic), "%s/system/state", _topicPrefix.c_str());
  _mqttClient.publish(topic, payload, true);
}

void MqttManager::publishSensorReading(const char* uid, float value,
                                       const char* type, const char* unit) {
  if (!_mqttClient.connected()) return;

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"%s\",\"value\":%.2f,\"type\":\"%s\",\"unit\":\"%s\"}",
           deviceName.c_str(), value, type ? type : "", unit ? unit : "");

  char topic[96];
  snprintf(topic, sizeof(topic), "%s/sensor/%s", _topicPrefix.c_str(), uid);
  _mqttClient.publish(topic, payload, true);
}

void MqttManager::publishAllSensorReadings() {
  if (!_mqttClient.connected()) return;

  uint8_t count = Modules.registeredCount();
  for (uint8_t i = 0; i < count; i++) {
    ModuleInfo info;
    if (!Modules.getRegistered(i, info)) continue;
    if (info.role != ROLE_SENSOR) continue;
    if (!info.publish) continue;  // module opted out of MQTT publishing

    SensorResponse resp;
    if (!Modules.readModule(info.addr, resp)) continue;  // skip on I²C/sensor error

    publishSensorReading(info.uid, resp.value, info.type, info.unit);
  }
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
