#pragma once
#include <Preferences.h>
#include "BridgeConfig.h"
namespace bridge {
class SettingsManager {
  Preferences store;
 public:
  bool master = false;
  uint32_t peer = 0;
  Interface interfaceMode = Interface::Auto;
  Settings saved, active, proposed;
  bool pending = false, trial = false;
  uint32_t switchAt = 0, trialUntil = 0;
  void begin() {
    store.begin("uni-bridge", false);
    master = store.getBool("master", false); peer = store.getUInt("peer", 0);
    saved.preset = store.getUChar("preset", 1); saved.baud = store.getUInt("baud", DefaultBaud);
    if (saved.preset > 2) saved.preset = 1;
    if (!validBaud(saved.baud)) saved.baud = DefaultBaud;
    uint8_t mode = store.getUChar("iface", 0);
    interfaceMode = mode <= 2 ? Interface(mode) : Interface::Auto;
    active = saved; active.preset = 1; active.token = 0; // Always rendezvous on Balanced after reboot.
  }
  void role(bool value) { master = value; store.putBool("master", value); }
  void pair(uint32_t value) { peer = value; store.putUInt("peer", peer); }
  void interface(Interface value) { interfaceMode = value; store.putUChar("iface", uint8_t(value)); }
  void stage(const Settings& value, uint32_t now) {
    proposed = value; pending = true; trial = false; switchAt = now + SwitchDelayMs;
  }
  void switched(uint32_t now) { active = proposed; trial = true; trialUntil = now + TrialMs; }
  void confirm() {
    saved = active; pending = trial = false;
    store.putUChar("preset", saved.preset); store.putUInt("baud", saved.baud);
  }
  void cancel() { pending = trial = false; }
};
}
