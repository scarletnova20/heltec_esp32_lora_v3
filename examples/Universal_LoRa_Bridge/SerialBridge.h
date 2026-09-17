#pragma once
#include <Arduino.h>
#include "BridgeConfig.h"
namespace bridge {
class SerialBridge {
  HardwareSerial uart{1};
  Interface mode = Interface::Auto, active = Interface::Usb;
  bool selected = false, pressured = false;
 public:
  ByteRing<BufferSize> incoming, outgoing;
  void begin(uint32_t baud, Interface selection);
  void setMode(Interface selection);
  void setBaud(uint32_t baud) { uart.updateBaudRate(baud); }
  void poll(Counters& stats);
  Interface getMode() const { return mode; }
  Interface getActive() const { return active; }
};
}
