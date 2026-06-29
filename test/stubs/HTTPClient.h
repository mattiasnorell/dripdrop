/**
 * Minimal HTTPClient stub for native unit testing.
 *
 * scenarios.cpp compiles its callUrl path unconditionally; these no-op methods
 * let it link. Scenario tests do not exercise real HTTP.
 */
#pragma once

#include <Arduino.h>

class HTTPClient {
public:
  void setTimeout(uint32_t) {}
  void addHeader(const String&, const String&) {}
  bool begin(const String&) { return true; }
  template <class T>
  bool begin(T&, const String&) { return true; }
  int GET() { return 200; }
  int POST(const String&) { return 200; }
  void end() {}
};
