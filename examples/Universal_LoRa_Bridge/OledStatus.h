#pragma once
#include <SSD1306Wire.h>
#include "BridgeConfig.h"
#include "ButtonMenu.h"
namespace bridge {
inline void drawMessage(SSD1306Wire& display, const OledMessage& msg, const char* title) {
  display.drawString(0, 0, title);
  char text[MessageMax + 1];
  snprintf(text, sizeof(text), "Node %08lX", (unsigned long)msg.node);
  display.drawString(0, 13, text);
  // Format only the OLED's bounded copy, never a radio/serial transport buffer.
  for (size_t i = 0; i < msg.length; ++i) text[i] = msg.data[i] >= 32 && msg.data[i] <= 126 ? char(msg.data[i]) : '.';
  text[msg.length] = 0;
  display.drawStringMaxWidth(0, 27, 128, text);
}
inline void drawStatus(SSD1306Wire& display, const Snapshot& s, const ButtonMenu& menu) {
  display.clear(); display.setTextAlignment(TEXT_ALIGN_LEFT); display.setFont(ArialMT_Plain_10);
  char text[64];
  const uint32_t heldMs = menu.heldMs(s.now);
  if (heldMs > 500) {
    display.drawString(0, 0, menu.isOpen() ? "Release at 2s: Back" : "Release: 2s = menu");
    if (!menu.isOpen()) {
      display.drawString(0, 14, "6s = change role");
      display.drawString(0, 28, "8s = unpair");
    }
    display.drawString(0, 43, String(heldMs / 1000) + " seconds held");
  } else if (menu.isOpen()) {
    display.drawString(0, 0, "LoRa menu");
    display.drawString(0, 16, menu.sendSelected() ? "  Back" : "> Back");
    display.drawString(0, 29, menu.sendSelected() ? "> Send message" : "  Send message");
    display.drawString(0, 43, "Click: next");
    display.drawString(0, 53, "Double-click: select");
  } else if (s.receivedMessage.state == MessageState::Received && uint32_t(s.now - s.receivedMessage.timestamp) < MessageToastMs) {
    drawMessage(display, s.receivedMessage, "RX message");
  } else if (s.sentMessage.state != MessageState::None && uint32_t(s.now - s.sentMessage.timestamp) < MessageToastMs) {
    if (s.sentMessage.state == MessageState::Rejected) {
      display.drawString(0, 0, "Send unavailable");
      display.drawStringMaxWidth(0, 16, 128, s.status);
    } else {
      const char* title = "TX queued";
      if (s.sentMessage.state == MessageState::Sending) title = "TX sending...";
      if (s.sentMessage.state == MessageState::Delivered) title = "TX delivered";
      if (s.sentMessage.state == MessageState::Unconfirmed) title = "TX unconfirmed";
      drawMessage(display, s.sentMessage, title);
    }
  } else {
    snprintf(text, sizeof(text), "%s %08lX", s.master ? "MASTER" : "REMOTE", (unsigned long)s.local);
    display.drawString(0, 0, text);
    switch ((s.now / 4000) % 5) {
      case 0:
        display.drawString(0, 13, s.linked ? "Link connected" : "Discovering / waiting");
        display.drawString(0, 26, String(Presets[s.preset].name) + " / " + (s.activeInterface == 2 ? "UART" : "USB"));
        display.drawString(0, 39, s.master ? "Web: 192.168.4.1" : "Wi-Fi OFF");
        display.drawString(0, 51, "Hold 2s: message menu"); break;
      case 1:
        snprintf(text, sizeof(text), "TX %lu / RX %lu", (unsigned long)s.counters.txPackets, (unsigned long)s.counters.rxPackets); display.drawString(0, 15, text);
        snprintf(text, sizeof(text), "Retry %lu / Dup %lu", (unsigned long)s.counters.retries, (unsigned long)s.counters.duplicates); display.drawString(0, 30, text);
        snprintf(text, sizeof(text), "Unconfirmed %lu", (unsigned long)s.counters.failed); display.drawString(0, 45, text); break;
      case 2:
        snprintf(text, sizeof(text), "RSSI %.1f SNR %.1f", s.rssi, s.snr); display.drawString(0, 15, text);
        snprintf(text, sizeof(text), "B/s TX %lu RX %lu", (unsigned long)s.txRate, (unsigned long)s.rxRate); display.drawString(0, 30, text);
        snprintf(text, sizeof(text), "Queues %u / %u", s.txQueued, s.rxQueued); display.drawString(0, 45, text); break;
      case 3:
        display.drawStringMaxWidth(0, 14, 128, s.status); break;
      default:
        display.clear();
        if (s.receivedMessage.state == MessageState::Received) drawMessage(display, s.receivedMessage, "Last RX message");
        else {
          display.drawString(0, 0, "No test message yet");
          display.drawString(0, 18, "Hold PRG 2s for menu");
          display.drawString(0, 34, s.pairingOpen ? "Pairing window open" : "Double-click: pairing");
        }
        break;
    }
  }
  display.display();
}
}
