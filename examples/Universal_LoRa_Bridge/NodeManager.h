#pragma once
#include "BridgeConfig.h"
namespace bridge {
class NodeManager {
 public:
  Node nodes[NodeCount]{};
  void heard(const Packet& p, uint32_t now, float rssi, float snr) {
    Node* target = nullptr;
    for (auto& n : nodes) if (n.id == p.source) { target = &n; break; }
    if (!target) for (auto& n : nodes) if (!n.id) { target = &n; break; }
    if (!target) {
      target = &nodes[0];
      for (auto& n : nodes) if (uint32_t(now - n.lastHeard) > uint32_t(now - target->lastHeard)) target = &n;
    }
    if (target->id != p.source) *target = Node{};
    target->id = p.source; target->session = p.session; target->lastHeard = now;
    target->rssi = rssi; target->snr = snr;
    if (p.kind == Kind::Hello && p.length == 14) {
      target->master = p.data[0] != 0; target->peer = get32(p.data + 1);
    }
  }
  const Node* find(uint32_t id) const { for (const auto& n : nodes) if (n.id == id) return &n; return nullptr; }
};
}
