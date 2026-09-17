#pragma once
#include "BridgeProtocol.h"
namespace bridge {
// Set the same permitted frequency/power on both boards before flashing.
constexpr float FrequencyMHz = 866.3f;
constexpr int8_t PowerDbm = 0;
constexpr int UartRx = 4, UartTx = 5;
constexpr uint32_t DefaultBaud = 115200;
constexpr const char* ApPassword = "LoRaBridge32";
constexpr size_t BufferSize = 4096, LogDepth = 24, NodeCount = 8;
constexpr uint8_t MaxAttempts = 5;
constexpr uint32_t DiscoveryMs = 4000, PeerTimeoutMs = 30000;
constexpr uint32_t SwitchDelayMs = 15000, TrialMs = 45000;
constexpr size_t MessageMax = 48;
constexpr uint32_t MessageToastMs = 8000;
enum class MessageState : uint8_t { None, Queued, Sending, Delivered, Unconfirmed, Received, Rejected };
struct OledMessage {
  MessageState state = MessageState::None;
  uint32_t timestamp = 0, node = 0;
  uint8_t length = 0;
  uint8_t data[MessageMax]{};
};
struct Preset { const char* name; float bandwidth; uint8_t sf, cr; };
constexpr Preset Presets[] = {{"Long Range", 125.0f, 11, 7}, {"Balanced", 250.0f, 9, 5}, {"High Speed", 500.0f, 7, 5}};
inline bool validBaud(uint32_t n) { return n == 9600 || n == 19200 || n == 38400 || n == 57600 || n == 115200 || n == 230400; }
enum class Interface : uint8_t { Auto, Usb, Uart };
struct Settings { uint8_t preset = 1; uint32_t baud = DefaultBaud; uint32_t token = 0; };
struct Counters {
  uint32_t txPackets = 0, rxPackets = 0, txBytes = 0, rxBytes = 0;
  uint32_t acknowledged = 0, retries = 0, duplicates = 0, failed = 0, failedBytes = 0;
  uint32_t malformed = 0, radioErrors = 0, backpressure = 0, logDrops = 0;
  uint32_t serialOverflows = 0, commandRejects = 0;
};
struct Node {
  uint32_t id = 0, lastHeard = 0, session = 0, peer = 0;
  bool master = false;
  float rssi = 0, snr = 0;
};
struct Snapshot {
  Counters counters;
  OledMessage sentMessage, receivedMessage;
  Node nodes[NodeCount];
  uint32_t now = 0, local = 0, peer = 0, lastPacket = 0, linkSince = 0;
  uint32_t baud = DefaultBaud, txRate = 0, rxRate = 0;
  uint16_t txQueued = 0, rxQueued = 0, logQueued = 0;
  uint8_t preset = 1, interfaceMode = 0, activeInterface = 1;
  bool master = false, linked = false, seenPacket = false, settingsPending = false, pairingOpen = false;
  float rssi = 0, snr = 0;
  char status[96] = "Starting";
};
enum class CommandType : uint8_t { Send, Pair, Settings, Interface, Unpair, TestMessage };
struct Command {
  CommandType type = CommandType::Send;
  uint32_t value = 0, baud = 0;
  uint16_t length = 0;
  uint8_t data[PayloadMax]{};
};
} // namespace bridge
