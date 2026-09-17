#pragma once
#include <stdint.h>

namespace bridge {
enum class ButtonAction { None, SendMessage, ChangeRole, Unpair, OpenPairing, CycleInterface };

// Pure UI state machine. Short-click events come from HotButton's debouncer.
// No delays: the radio and serial loop keeps running while a menu is open.
class ButtonMenu {
  bool pressing = false, opened = false, selectSend = false;
  bool suppressClicks = false;
  uint32_t pressAt = 0, releasedAt = 0;
 public:
  bool isOpen() const { return opened; }
  bool sendSelected() const { return selectSend; }
  uint32_t heldMs(uint32_t now) const { return pressing ? uint32_t(now - pressAt) : 0; }

  ButtonAction update(bool down, bool singleClick, bool doubleClick, uint32_t now) {
    if (down && !pressing) { pressing = true; pressAt = now; }
    if (!down && pressing) {
      pressing = false;
      const uint32_t held = now - pressAt;
      if (held >= 2000) {
        suppressClicks = true; releasedAt = now;
        if (opened) { opened = false; return ButtonAction::None; }
        if (held >= 8000) return ButtonAction::Unpair;
        if (held >= 6000) return ButtonAction::ChangeRole;
        opened = true; selectSend = false;
        return ButtonAction::None;
      }
    }
    if (suppressClicks) {
      if (uint32_t(now - releasedAt) < 300) return ButtonAction::None;
      suppressClicks = false;
    }
    if (pressing) return ButtonAction::None;
    if (opened) {
      if (doubleClick) {
        opened = false;
        return selectSend ? ButtonAction::SendMessage : ButtonAction::None;
      }
      if (singleClick) selectSend = !selectSend;
      return ButtonAction::None;
    }
    if (doubleClick) return ButtonAction::OpenPairing;
    if (singleClick) return ButtonAction::CycleInterface;
    return ButtonAction::None;
  }
};
}
