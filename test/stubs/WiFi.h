/**
 * WiFi.h stub for native unit testing.
 */
#ifndef WIFI_H_STUB
#define WIFI_H_STUB

#include <cstdint>

class WiFiClient {
public:
  bool connect(const char*, uint16_t) { return false; }
  bool connected() { return false; }
  void stop() {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(uint8_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }
};

class WiFiClass {
public:
  const char* localIP() const { return "0.0.0.0"; }
  const char* macAddress(char* buf) const { if (buf) buf[0] = '\0'; return buf; }
};

extern WiFiClass WiFi;

#endif // WIFI_H_STUB
