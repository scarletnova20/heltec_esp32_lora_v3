// Board: Heltec WiFi LoRa 32 V3. USB CDC On Boot: Disabled (on-board USB/UART).
// Do not enable Serial debug output: Serial is exclusively the telemetry byte stream.
#include <Arduino.h>
#if ARDUINO_USB_CDC_ON_BOOT
#error "Use USB CDC On Boot: Disabled for the Heltec V3 on-board USB-to-UART connection."
#endif
#include <WiFi.h>
#include <esp_log.h>
#include <heltec_unofficial.h> // Exactly one translation unit owns these hardware instances.
#include "ReliableLink.h"
#include "WebDashboard.h"
#include "OledStatus.h"

using namespace bridge;
SettingsManager settings;
SerialBridge serialBridge;
TelemetryLog telemetry;
ReliableLink bridgeLink(radio, serialBridge, telemetry, settings);
WebDashboard dashboard(telemetry);
volatile bool radioIrq = false;
portMUX_TYPE irqMux = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR onRadioIrq() {
  portENTER_CRITICAL_ISR(&irqMux); radioIrq = true; portEXIT_CRITICAL_ISR(&irqMux);
}
uint32_t lastScreen = 0, pressAt = 0;
uint32_t ignoreClicksUntil = 0;
bool pressing = false;

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  WiFi.mode(WIFI_OFF);
  heltec_setup();
  settings.begin();
  serialBridge.begin(settings.active.baud, settings.interfaceMode);
  telemetry.begin();
  bridgeLink.begin();
  radio.setDio1Action(onRadioIrq);
  Snapshot s; bridgeLink.snapshot(s, millis());
  if (settings.master && !dashboard.begin(s.local)) {
    display.clear(); display.drawString(0, 0, "Dashboard startup failed"); display.display();
  }
}
void loop() {
  uint32_t now = millis();
  heltec_loop();
  bool down = digitalRead(BUTTON) == LOW;
  if (down && !pressing) { pressing = true; pressAt = now; }
  if (!down && pressing) {
    pressing = false;
    if (uint32_t(now - pressAt) >= 8000) {
      ignoreClicksUntil = now + 1000;
      Command c; c.type = CommandType::Unpair; bridgeLink.command(c, now);
    } else if (uint32_t(now - pressAt) >= 3000) {
      settings.role(!settings.master); ESP.restart();
    }
  }
  bool doubleClick = button.isDoubleClick(), singleClick = button.isSingleClick();
  if (doubleClick && due(now, ignoreClicksUntil)) bridgeLink.openPairing(now);
  if (singleClick && due(now, ignoreClicksUntil)) {
    Command c; c.type = CommandType::Interface;
    c.value = (uint8_t(serialBridge.getMode()) + 1) % 3; bridgeLink.command(c, now);
  }
  portENTER_CRITICAL(&irqMux); bool irq = radioIrq; radioIrq = false; portEXIT_CRITICAL(&irqMux);
  bridgeLink.tick(irq, now);
  Command c; if (settings.master && dashboard.command(c)) bridgeLink.command(c, now);
  if (uint32_t(now - lastScreen) >= 250) {
    lastScreen = now; Snapshot s; bridgeLink.snapshot(s, now);
    if (settings.master) dashboard.publish(s);
    drawStatus(display, s, pressing ? uint32_t(now - pressAt) : 0);
  }
  delay(1); // Scheduler yield; no network calls or payload formatting in the transport loop.
}
