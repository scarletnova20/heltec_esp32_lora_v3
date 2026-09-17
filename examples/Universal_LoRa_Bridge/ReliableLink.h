#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "SerialBridge.h"
#include "TelemetryLog.h"
#include "NodeManager.h"
#include "SettingsManager.h"
namespace bridge {
class ReliableLink {
  SX1262& radio;
  SerialBridge& serial;
  TelemetryLog& log;
  SettingsManager& settings;
  NodeManager discovery;
  Counters stats;
  uint32_t id = 0, session = 0, sequence = 1, peerSession = 0;
  uint32_t nextHello = 0, nextSend = 0, ackDeadline = 0, radioDeadline = 0;
  uint32_t lastPeer = 0, linkSince = 0, lastPacket = 0, pairUntil = 0;
  uint32_t configSince = 0;
  uint32_t peerBaud = DefaultBaud;
  uint32_t sampleAt = 0, sampleTx = 0, sampleRx = 0, txRate = 0, rxRate = 0;
  bool ready = false, transmitting = false, waiting = false, havePending = false;
  bool scanning = false, channelReady = false;
  bool haveAck = false, seenPacket = false, linked = false;
  uint8_t attempts = 0;
  Packet pending, ack;
  ReceiveWindow received;
  uint8_t wire[FrameMax]{};
  float rssi = 0, snr = 0;
  char status[96] = "Starting";
  void message(const char* s) { snprintf(status, sizeof(status), "%s", s); }
  Packet packet(Kind kind);
  bool send(const Packet& p, uint32_t now);
  bool channelClear(uint32_t now);
  void listen();
  void receive(uint32_t now);
  void acknowledge(const Packet& p);
  void startPending(const Packet& p, uint32_t now);
  void complete(bool success, uint32_t now);
  bool applyPreset(uint8_t preset);
  void observe(bool tx, const uint8_t* bytes, size_t length, uint32_t now);
  uint32_t ackTimeout();
  void fallback(uint32_t now);
 public:
  ReliableLink(SX1262& r, SerialBridge& s, TelemetryLog& l, SettingsManager& c)
    : radio(r), serial(s), log(l), settings(c) {}
  bool begin();
  void tick(bool irq, uint32_t now);
  void command(const Command& cmd, uint32_t now);
  void snapshot(Snapshot& out, uint32_t now);
  void openPairing(uint32_t now) { pairUntil = now + 60000; message("Pairing open for 60 seconds"); }
};
}
