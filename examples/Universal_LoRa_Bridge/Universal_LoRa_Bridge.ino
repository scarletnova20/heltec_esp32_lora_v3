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
uint32_t lastScreen = 0;
ButtonMenu buttonMenu;

void showStartup(const char* stage) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Universal LoRa Bridge");
  display.drawString(0, 18, stage);
  display.display();
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  // V3.2 powers its OLED from Vext. Enable it BEFORE heltec_setup resets
  // and initializes the display; the upstream helper only does this for Stick.
  heltec_ve(true);
  delay(20); // Power-rail settling during startup only.
  heltec_setup();
  showStartup("Loading settings...");
  settings.begin();
  showStartup("Starting serial...");
  serialBridge.begin(settings.active.baud, settings.interfaceMode);
  telemetry.begin();
  showStartup("Starting LoRa...");
  bridgeLink.begin();
  radio.setDio1Action(onRadioIrq);
  Snapshot s; bridgeLink.snapshot(s, millis());
  showStartup(settings.master ? "Starting Master Wi-Fi..." : "Remote: Wi-Fi off");
  WiFi.mode(WIFI_OFF);
  if (settings.master && !dashboard.begin(s.local)) {
    display.clear(); display.drawString(0, 0, "Dashboard startup failed"); display.display();
  }
}
void loop() {
  uint32_t now = millis();
  heltec_loop();
  bool doubleClick = button.isDoubleClick(), singleClick = button.isSingleClick();
  const ButtonAction action = buttonMenu.update(digitalRead(BUTTON) == LOW, singleClick, doubleClick, now);
  Command buttonCommand;
  switch (action) {
    case ButtonAction::ChangeRole: settings.role(!settings.master); ESP.restart(); break;
    case ButtonAction::Unpair: buttonCommand.type = CommandType::Unpair; bridgeLink.command(buttonCommand, now); break;
    case ButtonAction::OpenPairing: bridgeLink.openPairing(now); break;
    case ButtonAction::CycleInterface:
      buttonCommand.type = CommandType::Interface;
      buttonCommand.value = (uint8_t(serialBridge.getMode()) + 1) % 3;
      bridgeLink.command(buttonCommand, now); break;
    case ButtonAction::SendMessage:
      buttonCommand.type = CommandType::TestMessage; bridgeLink.command(buttonCommand, now); break;
    default: break;
  }
  portENTER_CRITICAL(&irqMux); bool irq = radioIrq; radioIrq = false; portEXIT_CRITICAL(&irqMux);
  bridgeLink.tick(irq, now);
  Command c; if (settings.master && dashboard.command(c)) bridgeLink.command(c, now);
  if (uint32_t(now - lastScreen) >= 250) {
    lastScreen = now; Snapshot s; bridgeLink.snapshot(s, now);
    if (settings.master) dashboard.publish(s);
    drawStatus(display, s, buttonMenu);
  }
  delay(1); // Scheduler yield; no network calls or payload formatting in the transport loop.
}
