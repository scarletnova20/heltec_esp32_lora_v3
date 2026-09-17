#include <cassert>
#include <iostream>
#include "SerialBridge.h"
uint32_t testNow=0;
FakeEsp ESP;
HardwareSerial Serial(0);
using namespace bridge;
int main() {
  SerialBridge serial; Counters counters;
  serial.begin(57600,Interface::Auto);
  auto& uart=*HardwareSerial::ports[1];
  const uint8_t input[]={0,0xff,13,10,'T','X',':'};
  for(auto b:input)uart.rx.push_back(b);
  serial.poll(counters);
  assert(serial.getActive()==Interface::Uart);
  assert(Serial.written.empty()&&uart.written.empty()); // Input is never echoed.
  uint8_t bytes[32];assert(serial.incoming.peek(bytes,sizeof(bytes))==sizeof(input));
  assert(!memcmp(bytes,input,sizeof(input)));
  assert(serial.outgoing.push(input,sizeof(input)));
  uart.capacity=0;serial.poll(counters);assert(serial.outgoing.size()==sizeof(input));
  uart.capacity=3;serial.poll(counters);assert(uart.written.size()==3);
  uart.capacity=16;serial.poll(counters);
  assert(uart.written==std::vector<uint8_t>(input,input+sizeof(input)));
  assert(Serial.written.empty());
  Serial.rx.push_back(42);serial.poll(counters);assert(Serial.rx.size()==1); // Selection stays locked.
  serial.incoming.discard(serial.incoming.size());serial.setMode(Interface::Usb);serial.poll(counters);
  assert(serial.incoming.peek(bytes,1)==1&&bytes[0]==42);
  serial.incoming.discard(1);uint8_t full[BufferSize]{};assert(serial.incoming.push(full,sizeof(full)));
  Serial.rx.push_back(99);serial.poll(counters);assert(Serial.rx.size()==1&&counters.backpressure==1);
  Serial.onError(UART_FIFO_OVF_ERROR);serial.poll(counters);assert(counters.serialOverflows==1);
  assert(Serial.written.empty());
  std::cout<<"Actual serial adapter byte-preservation, no-echo, detection, bounded writes and overflow tests passed\n";
}
