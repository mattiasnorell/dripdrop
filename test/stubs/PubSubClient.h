/**
 * PubSubClient.h stub for native unit testing.
 */
#ifndef PUBSUBCLIENT_H_STUB
#define PUBSUBCLIENT_H_STUB

#include <cstdint>
#include "WiFi.h"

class PubSubClient {
public:
  PubSubClient() {}
  PubSubClient(WiFiClient&) {}
  bool connected() { return false; }
  bool connect(const char*, const char*, const char*, const char*, uint8_t, bool, const char*) { return false; }
  bool publish(const char*, const char*, bool = false) { return false; }
  bool subscribe(const char*) { return false; }
  void setServer(const char*, uint16_t) {}
  void setCallback(void(*)(char*, uint8_t*, unsigned int)) {}
  void setKeepAlive(uint16_t) {}
  void loop() {}
  void disconnect() {}
};

#endif // PUBSUBCLIENT_H_STUB
