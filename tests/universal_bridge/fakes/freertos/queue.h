#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
struct StaticQueue_t { size_t depth, itemSize, head, count; uint8_t* storage; };
using QueueHandle_t = StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t depth, size_t item, uint8_t* bytes, StaticQueue_t* q) {
  *q = {depth, item, 0, 0, bytes}; return q;
}
inline int xQueueSend(QueueHandle_t q, const void* p, unsigned timeout) {
  assert(timeout == 0);
  if (q->count == q->depth) return 0;
  memcpy(q->storage + ((q->head + q->count) % q->depth) * q->itemSize, p, q->itemSize); ++q->count; return 1;
}
inline int xQueueReceive(QueueHandle_t q, void* p, unsigned timeout) {
  assert(timeout == 0);
  if (!q->count) return 0;
  memcpy(p, q->storage + q->head * q->itemSize, q->itemSize); q->head = (q->head+1) % q->depth; --q->count; return 1;
}
inline size_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
