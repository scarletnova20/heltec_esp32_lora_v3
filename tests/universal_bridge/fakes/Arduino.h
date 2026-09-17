#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <vector>
#include <functional>
extern uint32_t testNow;
inline uint32_t millis() { return testNow; }
struct FakeEsp { uint64_t mac = 1; uint64_t getEfuseMac() { return mac; } };
extern FakeEsp ESP;
enum hardwareSerial_error_t { UART_NO_ERROR, UART_BUFFER_FULL_ERROR, UART_FIFO_OVF_ERROR };
constexpr unsigned SERIAL_8N1 = 0;
class HardwareSerial {
 public:
  inline static HardwareSerial* ports[3]{};
  std::deque<uint8_t> rx;
  std::vector<uint8_t> written;
  std::function<void(hardwareSerial_error_t)> onError;
  int capacity = 16;
  uint32_t baud = 0;
  explicit HardwareSerial(int id) { ports[id] = this; }
  void updateBaudRate(uint32_t value) { baud = value; }
  void begin(uint32_t value, unsigned = 0, int = 0, int = 0) { baud = value; }
  void end() {}
  void setRxBufferSize(size_t) {}
  void onReceiveError(std::function<void(hardwareSerial_error_t)> fn) { onError = fn; }
  int available() { return int(rx.size()); }
  int read() { if(rx.empty())return -1; int value=rx.front();rx.pop_front();return value; }
  int availableForWrite() { return capacity; }
  size_t write(const uint8_t* p,size_t n) { if(n>size_t(capacity))return 0; written.insert(written.end(),p,p+n);return n; }
};
extern HardwareSerial Serial;
