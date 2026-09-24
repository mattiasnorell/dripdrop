/**
 * DripDrop - Device Discovery
 *
 * Announces this device on the local network over UDP so a central discovery
 * service (and, through it, the React control app) can find it without the
 * user typing IP addresses.
 *
 * Protocol (UDP port DISCOVERY_PORT, default 4210):
 *   - On receiving {"type":"DISCOVER"} it unicasts an ANNOUNCE reply straight
 *     back to the sender.
 *   - Every DISCOVERY_HEARTBEAT_INTERVAL_MS it broadcasts an unsolicited
 *     ANNOUNCE to the subnet-directed broadcast address, so the service can
 *     recover after a missed reply or its own restart.
 *
 * ANNOUNCE payload (JSON):
 *   { "type":"ANNOUNCE", "mac", "hostname", "name", "ip", "fw_version" }
 *
 * STA-only: all activity is suppressed in AP mode and while disconnected.
 * Fully decoupled from the I2C (Modules) and HTTP (server) layers; the only
 * shared state it reads is the deviceName / apMode globals.
 *
 * handle() is non-blocking and safe to call every loop() iteration.
 */

#ifndef DRIPDROP_DEVICE_DISCOVERY_H
#define DRIPDROP_DEVICE_DISCOVERY_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"

class DeviceDiscovery {
public:
  // Start listening if already connected in STA mode, and send an initial
  // announce. Safe to call before WiFi is up — handle() starts lazily.
  void begin();

  // Non-blocking: drains any pending DISCOVER packet and emits the periodic
  // heartbeat. Call once per loop().
  void handle(unsigned long now);

private:
  void startSocket();
  void stopSocket();
  void handlePacket();
  void buildPayload(char* out, size_t outLen);
  void sendTo(IPAddress ip, uint16_t port);
  void broadcastAnnounce();
  IPAddress broadcastAddress() const;

  WiFiUDP _udp;
  bool _socketStarted = false;
  unsigned long _lastHeartbeat = 0;
};

extern DeviceDiscovery Discovery;

#endif  // DRIPDROP_DEVICE_DISCOVERY_H
