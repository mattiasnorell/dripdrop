/**
 * Minimal Wire.h stub for native unit testing.
 */
#ifndef WIRE_H_STUB
#define WIRE_H_STUB

#include <cstdint>

class TwoWire {
public:
  void begin() {}
  void setTimeout(uint16_t) {}
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  int read() { return 0; }
};

extern TwoWire Wire;

#endif // WIRE_H_STUB
