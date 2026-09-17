#pragma once
#include <stdint.h>
class Preferences {
 public:
  void begin(const char*, bool) {}
  bool getBool(const char*, bool value) { return value; }
  uint32_t getUInt(const char*, uint32_t value) { return value; }
  uint8_t getUChar(const char*, uint8_t value) { return value; }
  void putBool(const char*, bool) {}
  void putUInt(const char*, uint32_t) {}
  void putUChar(const char*, uint8_t) {}
};
