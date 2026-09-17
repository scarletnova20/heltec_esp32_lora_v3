#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace bridge {
constexpr size_t PayloadMax = 192;
constexpr size_t HeaderSize = 28;
constexpr size_t FrameMax = HeaderSize + PayloadMax + 4;
enum class Kind : uint8_t { Hello = 1, Pair, Data, Ack, Settings, Message };
struct Packet {
  Kind kind = Kind::Hello;
  uint32_t source = 0, destination = 0, session = 0, sequence = 0, ackSession = 0;
  uint16_t length = 0;
  uint8_t data[PayloadMax]{};
};
inline void put32(uint8_t* p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
inline size_t encode(const Packet& p, uint8_t* out, size_t capacity) {
  const size_t n = HeaderSize + p.length + 4;
  if (p.length > PayloadMax || capacity < n) return 0;
  out[0] = 'U'; out[1] = 'B'; out[2] = 1; out[3] = uint8_t(p.kind);
  out[4] = uint8_t(p.length); out[5] = uint8_t(p.length >> 8); out[6] = out[7] = 0;
  put32(out + 8, p.source); put32(out + 12, p.destination);
  put32(out + 16, p.session); put32(out + 20, p.sequence); put32(out + 24, p.ackSession);
  memcpy(out + HeaderSize, p.data, p.length);
  put32(out + n - 4, crc32(out, n - 4));
  return n;
}
inline bool decode(const uint8_t* in, size_t n, Packet& p) {
  if (n < HeaderSize + 4 || in[0] != 'U' || in[1] != 'B' || in[2] != 1 || in[6] || in[7]) return false;
  const size_t length = in[4] | uint16_t(in[5]) << 8;
  if (length > PayloadMax || n != HeaderSize + length + 4 ||
      in[3] < uint8_t(Kind::Hello) || in[3] > uint8_t(Kind::Message) ||
      crc32(in, n - 4) != get32(in + n - 4) || !get32(in + 8) || !get32(in + 16)) return false;
  p.kind = Kind(in[3]); p.length = uint16_t(length);
  p.source = get32(in + 8); p.destination = get32(in + 12);
  p.session = get32(in + 16); p.sequence = get32(in + 20); p.ackSession = get32(in + 24);
  memcpy(p.data, in + HeaderSize, length);
  return true;
}
inline bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
inline bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }

// Used only after the peer's boot session has been established by discovery.
struct ReceiveWindow {
  bool valid = false;
  uint32_t last = 0;
  bool duplicate(uint32_t sequence) const { return valid && !newer(sequence, last); }
  void accept(uint32_t sequence) { valid = true; last = sequence; }
  void reset() { valid = false; }
};
template<size_t Capacity> class ByteRing {
  uint8_t bytes[Capacity]{};
  size_t head = 0, used = 0;
 public:
  size_t size() const { return used; }
  size_t free() const { return Capacity - used; }
  bool push(const uint8_t* p, size_t n) {
    if (n > free()) return false;
    for (size_t i = 0; i < n; ++i) bytes[(head + used + i) % Capacity] = p[i];
    used += n; return true;
  }
  size_t peek(uint8_t* p, size_t n) const {
    if (n > used) n = used;
    for (size_t i = 0; i < n; ++i) p[i] = bytes[(head + i) % Capacity];
    return n;
  }
  void discard(size_t n) { if (n > used) n = used; head = (head + n) % Capacity; used -= n; }
};
} // namespace bridge
