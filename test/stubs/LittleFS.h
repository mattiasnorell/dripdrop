/**
 * Minimal LittleFS stub for native unit testing.
 * LittleFS.open() always returns a falsy File so load() starts empty.
 */
#ifndef LITTLEFS_H_STUB
#define LITTLEFS_H_STUB

#include <cstddef>

class File {
public:
  operator bool() const { return false; }
  void close() {}
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t len) { return len; }
  int read() { return -1; }
  size_t readBytes(char*, size_t) { return 0; }
  int available() { return 0; }
  size_t size() { return 0; }
};

class LittleFSClass {
public:
  bool begin(bool formatOnFail = false) { (void)formatOnFail; return true; }
  File open(const char*, const char* = "r") { return File(); }
};

extern LittleFSClass LittleFS;

#endif // LITTLEFS_H_STUB
