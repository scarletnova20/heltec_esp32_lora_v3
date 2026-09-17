#pragma once
#include <SSD1306Wire.h>
#include "BridgeConfig.h"
namespace bridge {
inline void drawStatus(SSD1306Wire& display, const Snapshot& s, uint32_t heldMs) {
  display.clear(); display.setTextAlignment(TEXT_ALIGN_LEFT); display.setFont(ArialMT_Plain_10);
  char text[64];
  if (heldMs > 500) {
    display.drawString(0, 0, "Release: 3s = change role");
    display.drawString(0, 16, "Release: 8s = unpair");
    display.drawString(0, 32, String(heldMs / 1000) + " seconds held");
  } else {
    snprintf(text, sizeof(text), "%s %08lX", s.master ? "MASTER" : "REMOTE", (unsigned long)s.local);
    display.drawString(0, 0, text);
    switch ((s.now / 4000) % 4) {
      case 0:
        display.drawString(0, 13, s.linked ? "Link connected" : "Discovering / waiting");
        display.drawString(0, 26, String(Presets[s.preset].name) + " / " + (s.activeInterface == 2 ? "UART" : "USB"));
        display.drawString(0, 39, s.master ? "Web: 192.168.4.1" : "Wi-Fi OFF");
        display.drawString(0, 51, s.pairingOpen ? "Pairing open" : "Double click: pair window"); break;
      case 1:
        snprintf(text, sizeof(text), "TX %lu / RX %lu", (unsigned long)s.counters.txPackets, (unsigned long)s.counters.rxPackets); display.drawString(0, 15, text);
        snprintf(text, sizeof(text), "Retry %lu / Dup %lu", (unsigned long)s.counters.retries, (unsigned long)s.counters.duplicates); display.drawString(0, 30, text);
        snprintf(text, sizeof(text), "Unconfirmed %lu", (unsigned long)s.counters.failed); display.drawString(0, 45, text); break;
      case 2:
        snprintf(text, sizeof(text), "RSSI %.1f SNR %.1f", s.rssi, s.snr); display.drawString(0, 15, text);
        snprintf(text, sizeof(text), "B/s TX %lu RX %lu", (unsigned long)s.txRate, (unsigned long)s.rxRate); display.drawString(0, 30, text);
        snprintf(text, sizeof(text), "Queues %u / %u", s.txQueued, s.rxQueued); display.drawString(0, 45, text); break;
      default:
        display.drawStringMaxWidth(0, 14, 128, s.status); break;
    }
  }
  display.display();
}
}
