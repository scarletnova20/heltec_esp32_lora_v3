#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_SYNC_WORD_PRIVATE 0x12
#define RADIOLIB_CHANNEL_FREE -15
#define RADIOLIB_LORA_DETECTED -16
class SX1262 {
 public:
  bool receiving = false, tx = false, irq = false, collision = false;
  bool scan = false, scanBusy = false;
  uint32_t finishAt = 0;
  float bw = 250; uint8_t sf = 9, cr = 5;
  std::vector<uint8_t> sent, inbox;
  int begin(float, float bandwidth, uint8_t spreading, uint8_t coding, uint8_t, int8_t) { bw=bandwidth; sf=spreading; cr=coding; return 0; }
  int setCRC(bool) { return 0; }
  int standby() { receiving = false; return 0; }
  int setBandwidth(float v) { bw = v; return 0; }
  int setSpreadingFactor(uint8_t v) { sf = v; return 0; }
  int setCodingRate(uint8_t v) { cr = v; return 0; }
  int startReceive() { receiving = true; return 0; }
  int startTransmit(const uint8_t* p, size_t n);
  int startChannelScan();
  int getChannelScanResult() { return scanBusy ? RADIOLIB_LORA_DETECTED : RADIOLIB_CHANNEL_FREE; }
  int finishTransmit() { tx = false; return 0; }
  uint32_t getTimeOnAir(size_t n) { return (30+n*2)*1000; }
  size_t getPacketLength() { return inbox.size(); }
  int readData(uint8_t* p, size_t n) { if(n!=inbox.size())return -1; memcpy(p,inbox.data(),n);inbox.clear();return 0; }
  float getRSSI() { return -65; }
  float getSNR() { return 8; }
};
