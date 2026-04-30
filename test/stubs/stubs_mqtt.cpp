/**
 * MqttManager stub for native unit testing.
 */
#include "../../src/mqtt.h"

MqttManager Mqtt;

void MqttManager::begin() {}
void MqttManager::loop(unsigned long) {}
void MqttManager::publishValveState(uint8_t) {}
void MqttManager::publishAllValveStates() {}
void MqttManager::publishSystemStatus() {}
void MqttManager::publishSensorReading(const char*, float) {}
bool MqttManager::isConnected() const { return false; }
void MqttManager::setServer(const char* server, uint16_t port) { _server = server; _port = port; }
void MqttManager::setCredentials(const char* user, const char* password) { _user = user; _password = password; }
void MqttManager::disconnect() {}
