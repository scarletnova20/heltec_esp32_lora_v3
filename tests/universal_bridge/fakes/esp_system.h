#pragma once
#include <stdint.h>
inline uint32_t esp_random() { static uint32_t s = 937; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
