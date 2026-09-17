#include "ReliableLink.h"
#include <esp_system.h>
namespace bridge {
Packet ReliableLink::packet(Kind kind) {
  Packet p; p.kind = kind; p.source = id; p.destination = settings.peer; p.session = session; return p;
}
bool ReliableLink::applyPreset(uint8_t preset) {
  const auto& p = Presets[preset];
  radio.standby();
  bool ok = radio.setBandwidth(p.bandwidth) == RADIOLIB_ERR_NONE &&
    radio.setSpreadingFactor(p.sf) == RADIOLIB_ERR_NONE && radio.setCodingRate(p.cr) == RADIOLIB_ERR_NONE;
  if (!ok) { ++stats.radioErrors; message("RF configuration failed"); }
  return ok;
}
bool ReliableLink::begin() {
  uint64_t mac = ESP.getEfuseMac(); id = uint32_t(mac) ^ uint32_t(mac >> 32); if (!id) id = 1;
  session = esp_random(); if (!session) session = 1;
  const auto& p = Presets[1];
  ready = radio.begin(FrequencyMHz, p.bandwidth, p.sf, p.cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, PowerDbm) == RADIOLIB_ERR_NONE;
  if (!ready) { message("Radio init failed; check hardware"); return false; }
  radio.setCRC(true);
  pairUntil = millis() + 60000; nextHello = millis() + esp_random() % 1000;
  message(settings.master ? "Master: select a discovered Remote" : "Remote: hold button 6s to become Master");
  listen(); return true;
}
void ReliableLink::listen() {
  if (radio.startReceive() != RADIOLIB_ERR_NONE) { ++stats.radioErrors; message("Radio RX start failed"); }
}
uint32_t ReliableLink::ackTimeout() {
  return uint32_t(radio.getTimeOnAir(FrameMax) / 1000) + uint32_t(radio.getTimeOnAir(HeaderSize + 4) / 1000) + 600;
}
bool ReliableLink::send(const Packet& p, uint32_t now) {
  size_t n = encode(p, wire, sizeof(wire)); if (!n) return false;
  radio.standby();
  if (radio.startTransmit(wire, n) != RADIOLIB_ERR_NONE) { ++stats.radioErrors; listen(); return false; }
  transmitting = true;
  radioDeadline = now + uint32_t(radio.getTimeOnAir(n) / 1000) + 1000;
  return true;
}
bool ReliableLink::channelClear(uint32_t now) {
  if (channelReady) { channelReady = false; return true; }
  if (radio.startChannelScan() != RADIOLIB_ERR_NONE) {
    ++stats.radioErrors; listen(); nextSend = now + 100 + esp_random() % 1000; return false;
  }
  scanning = true; radioDeadline = now + ackTimeout(); return false;
}
void ReliableLink::observe(bool tx, const uint8_t* bytes, size_t length, uint32_t now) {
  if (settings.master && !log.copy(tx, bytes, length, now)) ++stats.logDrops;
}
void ReliableLink::startPending(const Packet& p, uint32_t now) {
  pending = p; pending.sequence = sequence++; havePending = true; waiting = false; attempts = 0;
  nextSend = now + 20 + esp_random() % 300;
}
void ReliableLink::acknowledge(const Packet& p) {
  ack = packet(Kind::Ack); ack.destination = p.source; ack.sequence = p.sequence;
  ack.ackSession = p.session; haveAck = true;
}
void ReliableLink::complete(bool success, uint32_t now) {
  if (pending.kind == Kind::Data || pending.kind == Kind::Message) {
    if (success) ++stats.acknowledged;
    else { ++stats.failed; stats.failedBytes += pending.length; message("Retry limit: payload delivery unconfirmed"); }
    if (pending.kind == Kind::Message) {
      sentMessage.state = success ? MessageState::Delivered : MessageState::Unconfirmed;
      sentMessage.timestamp = now;
      message(success ? "Test message delivered" : "Test message delivery unconfirmed");
    }
  } else if (pending.kind == Kind::Settings) {
    if (success) {
      Settings value; value.preset = pending.data[0]; value.baud = get32(pending.data + 1); value.token = get32(pending.data + 5);
      settings.stage(value, now); message("Settings acknowledged; RF change scheduled");
    } else message("Settings ACK missing; rendezvous recovery enabled");
  } else if (pending.kind == Kind::Pair) {
    message(success ? "Pair request acknowledged" : "Pair ACK missing; check Remote and pairing window");
  }
  havePending = waiting = false; nextSend = now + 100 + esp_random() % 400;
}
void ReliableLink::receive(uint32_t now) {
  const size_t n = radio.getPacketLength();
  uint8_t bytes[FrameMax];
  if (n > sizeof(bytes) || n < HeaderSize + 4) { ++stats.malformed; listen(); return; }
  int state = radio.readData(bytes, n);
  float packetRssi = radio.getRSSI(), packetSnr = radio.getSNR();
  listen();
  Packet p;
  if (state != RADIOLIB_ERR_NONE || !decode(bytes, n, p)) { ++stats.malformed; return; }
  if (p.source == id) return;
  discovery.heard(p, now, packetRssi, packetSnr);
  if (p.destination && p.destination != id) return;
  if (p.kind == Kind::Hello) {
    if (p.length != 14 || p.data[0] > 1 || p.data[5] > 2 || !validBaud(get32(p.data + 6))) return;
    if (p.source != settings.peer || bool(p.data[0]) == settings.master || get32(p.data + 1) != id) return;
    if (peerSession != p.session) {
      peerSession = p.session; received.reset();
      // A pending payload may have been delivered before the peer rebooted; don't replay it into a new session.
      if (havePending && (pending.kind == Kind::Data || pending.kind == Kind::Message)) complete(false, now);
    }
    if (p.data[5] != settings.active.preset || get32(p.data + 10) != settings.active.token ||
        (settings.active.token && get32(p.data + 6) != settings.active.baud)) return;
    peerBaud = get32(p.data + 6);
    lastPeer = now; rssi = packetRssi; snr = packetSnr; lastPacket = now; seenPacket = true;
    if (!linked) { linked = true; linkSince = now; message("Peer connected"); }
    if (settings.trial) { settings.confirm(); message("Settings confirmed by peer"); }
    return;
  }
  if (p.kind == Kind::Pair) {
    if (settings.master || p.length || !p.destination ||
        (settings.peer != p.source && (due(now, pairUntil) || settings.peer))) return;
    if (settings.peer != p.source) settings.pair(p.source);
    if (peerSession != p.session) { peerSession = p.session; received.reset(); }
    acknowledge(p); nextHello = now + 200; message("Paired; awaiting peer heartbeat"); return;
  }
  if (p.source != settings.peer || !p.destination || p.session != peerSession) return;
  lastPeer = now; rssi = packetRssi; snr = packetSnr; lastPacket = now; seenPacket = true;
  if (p.kind == Kind::Ack) {
    if (!p.length && havePending && p.ackSession == session && p.sequence == pending.sequence && attempts) complete(true, now);
    return;
  }
  if (p.kind != Kind::Data && p.kind != Kind::Settings && p.kind != Kind::Message) return;
  if (p.kind == Kind::Message && (!p.length || p.length > MessageMax)) return;
  if (received.duplicate(p.sequence)) { ++stats.duplicates; acknowledge(p); return; }
  if (p.kind == Kind::Message) {
    // Explicit board-to-board messages never enter the transparent serial stream.
    receivedMessage.state = MessageState::Received; receivedMessage.timestamp = now;
    receivedMessage.node = p.source; receivedMessage.length = p.length;
    memcpy(receivedMessage.data, p.data, p.length);
    received.accept(p.sequence); acknowledge(p);
    ++stats.rxPackets; stats.rxBytes += p.length;
    observe(false, p.data, p.length, now);
  } else if (p.kind == Kind::Data) {
    if (!p.length) return;
    // Reserve downstream space BEFORE ACK. Never acknowledge bytes that cannot be retained.
    if (!serial.outgoing.push(p.data, p.length)) { ++stats.backpressure; return; }
    received.accept(p.sequence); acknowledge(p);
    ++stats.rxPackets; stats.rxBytes += p.length;
    observe(false, p.data, p.length, now);
  } else {
    if (settings.master || p.length != 9 || p.data[0] > 2 || !validBaud(get32(p.data + 1)) ||
        settings.pending || havePending || messageQueued || serial.incoming.size() || serial.outgoing.size()) return;
    Settings value; value.preset = p.data[0]; value.baud = get32(p.data + 1); value.token = get32(p.data + 5);
    if (!value.token) return;
    settings.stage(value, now); received.accept(p.sequence); acknowledge(p);
    message("Settings staged; RF change scheduled");
  }
}
void ReliableLink::fallback(uint32_t now) {
  settings.cancel(); settings.active = settings.saved; settings.active.preset = 1; settings.active.token = 0;
  configSince = now;
  applyPreset(1); serial.setBaud(settings.active.baud); listen(); linked = false;
  nextHello = now + esp_random() % 500; message("Balanced rendezvous: waiting for peer");
}
void ReliableLink::tick(bool irq, uint32_t now) {
  serial.poll(stats);
  if (messageQueued && uint32_t(now - sentMessage.timestamp) >= PeerTimeoutMs) {
    messageQueued = false; sentMessage.state = MessageState::Rejected; sentMessage.timestamp = now;
    message("Test message queue expired; try again");
  }
  if (!ready) return;
  if (transmitting) {
    if (!irq && !due(now, radioDeadline)) return;
    if (!irq) ++stats.radioErrors;
    radio.finishTransmit(); transmitting = false; listen();
    // Give the other side time to turn around before another control frame.
    nextSend = now + 40 + esp_random() % 160;
  } else if (scanning) {
    if (!irq && !due(now, radioDeadline)) return;
    scanning = false;
    channelReady = irq && radio.getChannelScanResult() == RADIOLIB_CHANNEL_FREE;
    listen();
    if (!channelReady) nextSend = now + 100 + esp_random() % ackTimeout();
    return;
  } else if (irq) { channelReady = false; receive(now); }
  if (linked && uint32_t(now - lastPeer) > PeerTimeoutMs) {
    linked = false;
    if (!settings.pending && settings.active.token) fallback(now);
    else message("Peer timed out");
  }
  if (settings.pending && !settings.trial && due(now, settings.switchAt) && !havePending && !haveAck) {
    if (applyPreset(settings.proposed.preset)) {
      settings.switched(now); configSince = now; serial.setBaud(settings.active.baud); linked = false; listen();
      nextHello = now + esp_random() % 500; message("Testing synchronized settings");
    } else fallback(now);
  }
  if (settings.trial && due(now, settings.trialUntil)) fallback(now);
  // Also recover if one side confirmed a trial but its confirmation was lost.
  if (!settings.pending && settings.active.token && !linked && uint32_t(now - configSince) > TrialMs) fallback(now);
  if (haveAck) { if (send(ack, now)) haveAck = false; return; }
  if (waiting && due(now, ackDeadline)) {
    waiting = false;
    if (attempts >= MaxAttempts) complete(false, now);
    else {
      uint32_t window = (1u << attempts) * ackTimeout();
      if (window > PeerTimeoutMs / 4) window = PeerTimeoutMs / 4;
      nextSend = now + 50 + esp_random() % window;
    }
  }
  if (!due(now, nextSend)) return;
  if (havePending && !waiting) {
    if (!channelClear(now)) return;
    ++attempts;
    if (attempts > 1) ++stats.retries;
    bool started = send(pending, now);
    waiting = true;
    ackDeadline = now + (started ? uint32_t(radio.getTimeOnAir(HeaderSize + pending.length + 4) / 1000) : 0) + ackTimeout();
    return;
  }
  if (!waiting && due(now, nextHello)) {
    if (!channelClear(now)) return;
    Packet p = packet(Kind::Hello); p.destination = 0; p.length = 14;
    p.data[0] = settings.master; put32(p.data + 1, settings.peer); p.data[5] = settings.active.preset;
    put32(p.data + 6, settings.active.baud); put32(p.data + 10, settings.active.token);
    send(p, now); nextHello = now + DiscoveryMs + esp_random() % 1000; return;
  }
  if (!havePending && linked && !settings.pending && messageQueued) {
    messageQueued = false;
    ++stats.txPackets; stats.txBytes += queuedMessage.length;
    observe(true, queuedMessage.data, queuedMessage.length, now);
    sentMessage.state = MessageState::Sending; sentMessage.timestamp = now;
    startPending(queuedMessage, now);
  } else if (!havePending && linked && !settings.pending && serial.incoming.size()) {
    Packet p = packet(Kind::Data); p.length = serial.incoming.peek(p.data, PayloadMax);
    serial.incoming.discard(p.length);
    ++stats.txPackets; stats.txBytes += p.length; observe(true, p.data, p.length, now);
    startPending(p, now);
  }
  // Restore the saved RF preset after both rebooted onto the common rendezvous profile.
  if (settings.master && linked && !havePending && !settings.pending && settings.active.token == 0 &&
      (settings.saved.preset != 1 || settings.saved.baud != settings.active.baud || peerBaud != settings.saved.baud) &&
      !serial.incoming.size() && !serial.outgoing.size()) {
    Command c; c.type = CommandType::Settings; c.value = settings.saved.preset; c.baud = settings.saved.baud; command(c, now);
  }
}
void ReliableLink::command(const Command& c, uint32_t now) {
  bool ok = false;
  if (c.type == CommandType::TestMessage) {
    if (!ready || !linked || !settings.peer || settings.pending || messageQueued ||
        (havePending && pending.kind != Kind::Data)) {
      ++stats.commandRejects;
      // Do not replace the display state of an already queued/in-flight message.
      if (!messageQueued && !(havePending && pending.kind == Kind::Message)) {
        sentMessage = OledMessage{}; sentMessage.state = MessageState::Rejected; sentMessage.timestamp = now;
      }
      message(!linked ? "Pair and connect before sending" : "Link busy; try Send message again");
      return;
    }
    queuedMessage = packet(Kind::Message);
    char text[MessageMax + 1];
    queuedMessage.length = snprintf(text, sizeof(text), "Hello from %08lX", (unsigned long)id);
    memcpy(queuedMessage.data, text, queuedMessage.length);
    sentMessage = OledMessage{}; sentMessage.state = MessageState::Queued; sentMessage.timestamp = now;
    sentMessage.node = settings.peer; sentMessage.length = queuedMessage.length;
    memcpy(sentMessage.data, queuedMessage.data, queuedMessage.length);
    messageQueued = true; message("Test message queued"); ok = true;
  } else if (c.type == CommandType::Send) {
    ok = c.length && c.length <= PayloadMax && serial.incoming.push(c.data, c.length);
    if (ok) message("Web payload queued (no serial echo)");
  } else if (c.type == CommandType::Pair && settings.master && !havePending && !settings.pending && !settings.peer) {
    const Node* node = discovery.find(c.value);
    if (node && !node->master && !node->peer && uint32_t(now - node->lastHeard) < PeerTimeoutMs) {
      settings.pair(node->id); peerSession = node->session; received.reset();
      Packet p = packet(Kind::Pair); startPending(p, now); ok = true;
    }
  } else if (c.type == CommandType::Unpair && !havePending && !messageQueued && !settings.pending && !serial.incoming.size() && !serial.outgoing.size()) {
    settings.pair(0); peerSession = 0; received.reset(); linked = false; fallback(now); openPairing(now); ok = true;
  } else if (c.type == CommandType::Interface && c.value <= 2 && !serial.incoming.size() && !serial.outgoing.size() && !havePending) {
    settings.interface(Interface(c.value)); serial.setMode(Interface(c.value)); message("Serial interface selection updated"); ok = true;
  } else if (c.type == CommandType::Settings && settings.master && linked && !havePending && !messageQueued && !settings.pending &&
             !serial.incoming.size() && !serial.outgoing.size() && c.value <= 2 && validBaud(c.baud)) {
    Packet p = packet(Kind::Settings); p.length = 9; p.data[0] = c.value;
    put32(p.data + 1, c.baud); put32(p.data + 5, esp_random() | 1u); startPending(p, now); ok = true;
  }
  if (!ok) { ++stats.commandRejects; message("Command rejected: check link, pairing or busy queues"); }
}
void ReliableLink::snapshot(Snapshot& out, uint32_t now) {
  if (uint32_t(now - sampleAt) >= 1000) {
    uint32_t elapsed = now - sampleAt;
    txRate = uint32_t(uint64_t(stats.txBytes - sampleTx) * 1000 / elapsed);
    rxRate = uint32_t(uint64_t(stats.rxBytes - sampleRx) * 1000 / elapsed);
    sampleAt = now; sampleTx = stats.txBytes; sampleRx = stats.rxBytes;
  }
  out = Snapshot{}; out.counters = stats; memcpy(out.nodes, discovery.nodes, sizeof(out.nodes));
  out.sentMessage = sentMessage; out.receivedMessage = receivedMessage;
  out.now = now; out.local = id; out.peer = settings.peer; out.lastPacket = lastPacket; out.linkSince = linkSince;
  out.baud = settings.active.baud; out.txRate = txRate; out.rxRate = rxRate;
  out.txQueued = serial.incoming.size() + (havePending && pending.kind == Kind::Data ? pending.length : 0);
  out.rxQueued = serial.outgoing.size(); out.logQueued = log.size(); out.preset = settings.active.preset;
  out.interfaceMode = uint8_t(serial.getMode()); out.activeInterface = uint8_t(serial.getActive());
  out.master = settings.master; out.linked = linked; out.seenPacket = seenPacket;
  out.settingsPending = settings.pending; out.pairingOpen = !due(now, pairUntil);
  out.rssi = rssi; out.snr = snr; snprintf(out.status, sizeof(out.status), "%s", status);
}
}
