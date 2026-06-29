/**
 * Minimal Arduino.h stub for native unit testing.
 */
#ifndef ARDUINO_H_STUB
#define ARDUINO_H_STUB

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <climits>
#include <string>

// Arduino type aliases (already in <cstdint> but some code expects them unqualified)
using std::size_t;
using byte = uint8_t;

// Pin constants
#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

// F() macro — passthrough on native
#define F(x) (x)

// Minimal Arduino String class
class String {
public:
  String() {}
  String(const char* s) : _str(s ? s : "") {}
  String(int val) : _str(std::to_string(val)) {}
  String(unsigned int val) : _str(std::to_string(val)) {}
  String(unsigned long val) : _str(std::to_string(val)) {}
  const char* c_str() const { return _str.c_str(); }
  size_t length() const { return _str.length(); }
  bool operator==(const String& other) const { return _str == other._str; }
  bool operator==(const char* other) const { return _str == (other ? other : ""); }
  bool operator!=(const String& other) const { return _str != other._str; }
  bool operator!=(const char* other) const { return _str != (other ? other : ""); }
  String operator+(const String& other) const { return String((_str + other._str).c_str()); }
  String& operator+=(const String& other) { _str += other._str; return *this; }
  operator const char*() const { return _str.c_str(); }

  void reserve(size_t) {}

  String substring(int from) const {
    if (from < 0) from = 0;
    if ((size_t)from >= _str.size()) return String();
    return String(_str.substr(from).c_str());
  }
  String substring(int from, int to) const {
    if (from < 0) from = 0;
    if (to < from) to = from;
    if ((size_t)from >= _str.size()) return String();
    size_t len = (size_t)to > _str.size() ? _str.size() - from : (size_t)(to - from);
    return String(_str.substr(from, len).c_str());
  }
  int indexOf(char c) const {
    auto p = _str.find(c);
    return p == std::string::npos ? -1 : (int)p;
  }
  int indexOf(char c, int from) const {
    if (from < 0) from = 0;
    auto p = _str.find(c, (size_t)from);
    return p == std::string::npos ? -1 : (int)p;
  }
  bool startsWith(const char* prefix) const { return _str.rfind(prefix, 0) == 0; }
  void trim() {
    size_t b = _str.find_first_not_of(" \t\r\n");
    size_t e = _str.find_last_not_of(" \t\r\n");
    _str = (b == std::string::npos) ? std::string() : _str.substr(b, e - b + 1);
  }

  // Stream-like interface for ArduinoJson
  size_t write(uint8_t c) { _str += static_cast<char>(c); return 1; }
  size_t write(const uint8_t* buf, size_t len) { _str.append(reinterpret_cast<const char*>(buf), len); return len; }
  int read() { if (_pos < _str.size()) return _str[_pos++]; return -1; }
  int peek() { if (_pos < _str.size()) return _str[_pos]; return -1; }
  int available() { return _str.size() - _pos; }
private:
  std::string _str;
  size_t _pos = 0;
};

// Serial stub
struct SerialStub {
  void begin(unsigned long) {}
  void print(const char*) {}
  void println(const char*) {}
  void printf(const char*, ...) {}
};
extern SerialStub Serial;

// Arduino functions
inline unsigned long millis() {
  static unsigned long ms = 0;
  return ms++;
}

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline uint8_t digitalRead(uint8_t) { return 0; }
inline void delay(unsigned long) {}

#endif // ARDUINO_H_STUB
