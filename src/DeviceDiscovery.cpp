/**
 * DripDrop - Device Discovery implementation
 */

#include "DeviceDiscovery.h"

#include <WiFi.h>
#include <ArduinoJson.h>

// Shared state defined in dripdrop.cpp (see api_utils.h). Re-declared locally
// to avoid pulling the WebServer-heavy api_utils.h into this network module,
// mirroring how mqtt.cpp references deviceName.
extern String deviceName;
extern bool apMode;

// Largest ANNOUNCE payload is comfortably under 200 bytes; round up for headroom.
static constexpr size_t DISCOVERY_PAYLOAD_MAX = 256;

DeviceDiscovery Discovery;

void DeviceDiscovery::begin() {
  // If we already have a station connection, start immediately and announce.
  // Otherwise handle() will start the socket the first time STA connectivity
  // is (re)acquired.
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    startSocket();
    broadcastAnnounce();
  }
}

void DeviceDiscovery::handle(unsigned long now) {
  // STA-only. In AP mode (or once the link drops) tear the socket down so it
  // is rebuilt — and a fresh announce sent — when the station link returns.
  if (apMode || WiFi.status() != WL_CONNECTED) {
    stopSocket();
    return;
  }

  if (!_socketStarted) {
    startSocket();
    broadcastAnnounce();  // self-heal announce on (re)connect
  }

  handlePacket();

  if (now - _lastHeartbeat >= DISCOVERY_HEARTBEAT_INTERVAL_MS) {
    broadcastAnnounce();
  }
}

void DeviceDiscovery::startSocket() {
  if (_socketStarted) return;
  _udp.begin(DISCOVERY_PORT);
  _socketStarted = true;
}

void DeviceDiscovery::stopSocket() {
  if (!_socketStarted) return;
  _udp.stop();
  _socketStarted = false;
}

void DeviceDiscovery::handlePacket() {
  int size = _udp.parsePacket();  // non-blocking: 0 when nothing is queued
  if (size <= 0) return;

  char buf[DISCOVERY_PAYLOAD_MAX];
  int len = _udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = '\0';

  // Capture the sender before parsing — a malformed packet still consumed it.
  IPAddress from = _udp.remoteIP();
  uint16_t fromPort = _udp.remotePort();

  JsonDocument doc;
  if (deserializeJson(doc, buf, len) != DeserializationError::Ok) return;

  if (doc["type"].is<const char*>() &&
      strcmp(doc["type"].as<const char*>(), "DISCOVER") == 0) {
    sendTo(from, fromPort);  // unicast reply straight back to the requester
  }
}

void DeviceDiscovery::buildPayload(char* out, size_t outLen) {
  JsonDocument doc;
  doc["type"]       = "ANNOUNCE";
  doc["mac"]        = WiFi.macAddress();
  doc["hostname"]   = String(MDNS_HOSTNAME) + ".local";
  doc["name"]       = deviceName;
  doc["ip"]         = WiFi.localIP().toString();
  doc["fw_version"] = FIRMWARE_VERSION;
  serializeJson(doc, out, outLen);
}

void DeviceDiscovery::sendTo(IPAddress ip, uint16_t port) {
  if (!_socketStarted) return;
  char payload[DISCOVERY_PAYLOAD_MAX];
  buildPayload(payload, sizeof(payload));
  _udp.beginPacket(ip, port);
  _udp.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
  _udp.endPacket();
}

void DeviceDiscovery::broadcastAnnounce() {
  sendTo(broadcastAddress(), DISCOVERY_PORT);
  _lastHeartbeat = millis();
}

IPAddress DeviceDiscovery::broadcastAddress() const {
  // Subnet-directed broadcast (host bits all 1s), recomputed each send so a
  // DHCP lease change is picked up. Preferred over 255.255.255.255 because
  // some WiFi drivers drop global broadcasts on the TX path.
  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress bcast;
  for (int i = 0; i < 4; i++) {
    bcast[i] = (ip[i] & mask[i]) | (~mask[i] & 0xFF);
  }
  return bcast;
}
