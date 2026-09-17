#include "SerialBridge.h"
#include <atomic>
namespace bridge {
static std::atomic<uint32_t> overruns{0};
void SerialBridge::begin(uint32_t baud, Interface selection) {
  // Heltec V3's USB connector uses the on-board USB-to-UART bridge (Serial0).
  Serial.end(); Serial.setRxBufferSize(BufferSize); Serial.begin(DefaultBaud);
  uart.setRxBufferSize(BufferSize); uart.begin(baud, SERIAL_8N1, UartRx, UartTx);
  auto error = [](hardwareSerial_error_t e) {
    if (e == UART_BUFFER_FULL_ERROR || e == UART_FIFO_OVF_ERROR) ++overruns;
  };
  Serial.onReceiveError(error); uart.onReceiveError(error);
  setMode(selection);
}
void SerialBridge::setMode(Interface selection) {
  mode = selection; selected = mode != Interface::Auto;
  active = mode == Interface::Uart ? Interface::Uart : Interface::Usb;
}
void SerialBridge::poll(Counters& stats) {
  stats.serialOverflows = overruns.load();
  // First activity wins until explicit reset/override. Do not consume or merge the other port.
  if (!selected) {
    if (Serial.available()) { active = Interface::Usb; selected = true; }
    else if (uart.available()) { active = Interface::Uart; selected = true; }
  }
  HardwareSerial& port = active == Interface::Uart ? uart : Serial;
  uint8_t bytes[PayloadMax]; size_t n = 0;
  const size_t free = incoming.free();
  while (n < sizeof(bytes) && n < free && port.available()) {
    int c = port.read(); if (c < 0) break; bytes[n++] = uint8_t(c);
  }
  if (n) incoming.push(bytes, n);
  const bool full = !incoming.free() && port.available();
  if (full && !pressured) ++stats.backpressure;
  pressured = full;
  // Hardware UART write is bounded by available TX FIFO space; never call flush().
  int available = port.availableForWrite();
  if (available > 0) {
    n = outgoing.peek(bytes, size_t(available) < sizeof(bytes) ? size_t(available) : sizeof(bytes));
    if (n) outgoing.discard(port.write(bytes, n));
  }
}
}
