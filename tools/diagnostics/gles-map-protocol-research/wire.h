#ifndef WIRE_H
#define WIRE_H
#include <stdint.h>
/* Standalone little-endian research wire, not the existing EMU protocol. */
enum {
  W_MAP = 1,
  W_FLUSH = 2,
  W_UNMAP = 3,
  W_ERROR = 4,
  W_BIND_TEST = 10,
  W_DRAW_TEST = 11
};
struct __attribute__((packed)) wire_req {
  uint32_t op, target;
  uint64_t id;
  int64_t offset, length;
  uint32_t flags;
};
struct __attribute__((packed)) wire_resp {
  int32_t status;
  uint64_t value;
};
#endif
