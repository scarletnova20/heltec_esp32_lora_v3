#pragma once
#include "TelemetryLog.h"
namespace bridge {
class WebDashboard {
  TelemetryLog& log;
  QueueHandle_t snapshots = nullptr, commands = nullptr;
  StaticQueue_t snapshotControl{}, commandControl{};
  uint8_t snapshotStorage[sizeof(Snapshot)]{}, commandStorage[8 * sizeof(Command)]{};
  static void task(void* arg);
  void run();
 public:
  explicit WebDashboard(TelemetryLog& l) : log(l) {}
  bool begin(uint32_t id);
  void publish(const Snapshot& snapshot) { if (snapshots) xQueueOverwrite(snapshots, &snapshot); }
  bool command(Command& cmd) { return commands && xQueueReceive(commands, &cmd, 0) == pdTRUE; }
};
}
