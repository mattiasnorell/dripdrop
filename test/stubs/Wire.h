/**
 * Wire.h stub for native unit testing.
 *
 * Supports programmable per-(address, command) responses so module-scan
 * and read logic can be exercised without real hardware.
 *
 * Usage:
 *   Wire.stub_setResponse(addr, cmd, data, len);  // program a response
 *   Wire.stub_reset();                             // clear all state
 */
#ifndef WIRE_H_STUB
#define WIRE_H_STUB

#include <cstdint>
#include <cstring>

// Maximum payload size for a single I²C response in the stub
static const uint8_t WIRE_STUB_BUF_SIZE  = 32;

// Maximum number of concurrently programmed (addr, cmd) responses
static const uint8_t WIRE_STUB_MAX_RESP  = 8;

class TwoWire {
public:
  // ---- Arduino Wire API -----------------------------------------------

  void begin() {}
  void setTimeout(uint16_t) {}
  void setTimeOut(uint16_t) {} // Arduino's actual spelling (capital O)

  /** Start a transmission to addr. */
  void beginTransmission(uint8_t addr) {
    _txAddr       = addr;
    _txCmd        = 0;
    _txPayload    = 0;
    _txWriteCount = 0;
  }

  /** Write bytes into the pending transmission. First byte is the command. */
  size_t write(uint8_t b) {
    if (_txWriteCount == 0) {
      _txCmd = b;
    } else {
      _txPayload = b;
    }
    _txWriteCount++;
    return 1;
  }

  /**
   * End the transmission. Returns 0 (ACK) if a stub response has been
   * programmed for the current (addr, cmd) pair; 4 (NACK) otherwise.
   * The last transmitted addr/cmd are saved for requestFrom() to match on.
   */
  uint8_t endTransmission(bool = true) {
    _lastTxAddr = _txAddr;
    _lastTxCmd  = _txCmd;
    for (uint8_t i = 0; i < WIRE_STUB_MAX_RESP; i++) {
      if (_resp[i].valid &&
          _resp[i].addr == _txAddr &&
          _resp[i].cmd  == _txCmd) {
        return 0;  // ACK
      }
    }
    return 4;  // NACK — no device at this address
  }

  /**
   * Request bytes from addr. Returns the number of bytes placed into the
   * internal read buffer if a matching (addr, lastCmd) response exists;
   * 0 otherwise.
   */
  uint8_t requestFrom(uint8_t addr, uint8_t quantity) {
    for (uint8_t i = 0; i < WIRE_STUB_MAX_RESP; i++) {
      if (_resp[i].valid &&
          _resp[i].addr == addr &&
          _resp[i].cmd  == _lastTxCmd) {
        uint8_t n = (_resp[i].len < quantity) ? _resp[i].len : quantity;
        memcpy(_rxBuf, _resp[i].data, n);
        _rxLen = n;
        _rxPos = 0;
        return n;
      }
    }
    _rxLen = 0;
    _rxPos = 0;
    return 0;
  }

  /** Read one byte from the response buffer. Returns -1 when exhausted. */
  int read() {
    if (_rxPos >= _rxLen) return -1;
    return _rxBuf[_rxPos++];
  }

  int available() {
    return (int)(_rxLen - _rxPos);
  }

  // ---- Test helpers ---------------------------------------------------

  /**
   * Program a response: when the stub sees addr+cmd it returns data[0..len-1]
   * and ACKs the endTransmission().
   * Overwrites any existing entry for the same (addr, cmd).
   */
  void stub_setResponse(uint8_t addr, uint8_t cmd,
                        const uint8_t* data, uint8_t len) {
    // Find an existing slot for this (addr, cmd) or grab the first empty one
    for (uint8_t i = 0; i < WIRE_STUB_MAX_RESP; i++) {
      if (!_resp[i].valid ||
          (_resp[i].addr == addr && _resp[i].cmd == cmd)) {
        _resp[i].addr  = addr;
        _resp[i].cmd   = cmd;
        if (len > WIRE_STUB_BUF_SIZE) len = WIRE_STUB_BUF_SIZE;
        if (data && len > 0) memcpy(_resp[i].data, data, len);
        _resp[i].len   = len;
        _resp[i].valid = true;
        return;
      }
    }
  }

  /** Clear all programmed responses and reset internal state. */
  void stub_reset() {
    for (uint8_t i = 0; i < WIRE_STUB_MAX_RESP; i++) {
      _resp[i].valid = false;
      _resp[i].len   = 0;
    }
    _rxLen = 0;
    _rxPos = 0;
    _txAddr = _txCmd = _lastTxAddr = _lastTxCmd = 0;
    _txPayload    = 0;
    _txWriteCount = 0;
  }

  uint8_t stub_lastTxAddr()    const { return _lastTxAddr; }
  uint8_t stub_lastTxCmd()     const { return _lastTxCmd;  }
  uint8_t stub_lastTxPayload() const { return _txPayload;  }

private:
  struct StubResponse {
    uint8_t addr;
    uint8_t cmd;
    uint8_t data[WIRE_STUB_BUF_SIZE];
    uint8_t len;
    bool    valid;
  };

  StubResponse _resp[WIRE_STUB_MAX_RESP] = {};

  uint8_t _rxBuf[WIRE_STUB_BUF_SIZE] = {};
  uint8_t _rxLen       = 0;
  uint8_t _rxPos       = 0;

  uint8_t _txAddr        = 0;
  uint8_t _txCmd         = 0;
  uint8_t _txPayload     = 0;
  uint8_t _txWriteCount  = 0;
  uint8_t _lastTxAddr    = 0;
  uint8_t _lastTxCmd     = 0;
};

extern TwoWire Wire;

#endif // WIRE_H_STUB
