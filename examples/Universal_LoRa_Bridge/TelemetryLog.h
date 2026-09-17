#pragma once
#include <Arduino.h>
#include "BridgeConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
namespace bridge {
struct PayloadEvent {
  uint32_t timestamp;
  uint16_t length;
  bool tx;
  uint32_t source, destination, sequence;
  uint8_t outcome; // 0: payload, 1: receiver ACK, 2: delivery unconfirmed
  uint8_t data[PayloadMax];
};
class TelemetryLog {
  StaticQueue_t control{};
  uint8_t storage[LogDepth * sizeof(PayloadEvent)]{};
  QueueHandle_t queue = nullptr;
 public:
  void begin() { queue = xQueueCreateStatic(LogDepth, sizeof(PayloadEvent), storage, &control); }
  // No allocation, encoding, I/O, waits, or mutation of the caller's bytes.
  // A full observer queue drops the event immediately, never the transport data.
  bool copy(bool tx, const uint8_t* data, size_t length, uint32_t now,
            uint32_t source = 0, uint32_t destination = 0, uint32_t sequence = 0, uint8_t outcome = 0) {
    if (!queue || length > PayloadMax) return false;
    PayloadEvent event{}; event.tx = tx; event.timestamp = now; event.length = length;
    event.source = source; event.destination = destination; event.sequence = sequence; event.outcome = outcome;
    memcpy(event.data, data, length);
    return xQueueSend(queue, &event, 0) == pdTRUE;
  }
  bool pop(PayloadEvent& event) { return queue && xQueueReceive(queue, &event, 0) == pdTRUE; }
  size_t size() const { return queue ? uxQueueMessagesWaiting(queue) : 0; }
};
} // namespace bridge
