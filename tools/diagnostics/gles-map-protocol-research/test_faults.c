#include "pm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned pending;
static int mode, unmaps, flushes;
static unsigned char memory[32];
static unsigned get_error(void) {
  unsigned e = pending;
  pending = 0;
  return e;
}
static void *map(unsigned t, intptr_t o, intptr_t n, unsigned a) {
  (void)t;
  (void)o;
  (void)n;
  (void)a;
  if (mode == 1)
    pending = 0x502;
  return memory;
}
static void get_pointer(unsigned t, unsigned pname, void **p) {
  (void)t;
  (void)pname;
  *p = memory;
}
static void flush(unsigned t, intptr_t o, intptr_t n) {
  (void)t;
  (void)o;
  (void)n;
  flushes++;
  if (mode == 2) pending = 0x502;
}
static unsigned char unmap(unsigned t) {
  (void)t;
  unmaps++;
  if (mode == 3) { pending = 0x502; return 0; }
  return 1;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  struct pm *p = pm_new(get_error, map, get_pointer, flush, unmap);
  struct pm_ctx *c = pm_ctx_new(p, 1);
  uint64_t id = 0;
  if (!strcmp(argv[1], "host_map_rollback")) {
    mode = 1;
    int rc = pm_map(c, 1, 0, 16, 0x36, &id);
    if (rc == PM_GL_ERROR && id == 0 && unmaps == 1 &&
        pm_error(c) == 0x502 && pm_error(c) == 0)
      return 0;
    fprintf(stderr,
            "FAIL host map returned pointer plus error but rollback count=%d "
            "rc=%d\n",
            unmaps, rc);
    return 1;
  }
  if (!strcmp(argv[1], "flush_error")) {
    if (pm_map(c, 1, 0, 16, 0x36, &id) != PM_OK) return 2;
    mode = 2; char data = 42;
    int rc = pm_flush(c, 1, id, 0, &data, 1);
    if (rc == PM_GL_ERROR && pm_error(c) == 0x502 &&
        pm_flush(c, 1, id, 0, &data, 1) != PM_OK) return 0;
    fprintf(stderr, "FAIL flush error lost or incorrectly reported success\n"); return 1;
  }
  if (!strcmp(argv[1], "unmap_error")) {
    if (pm_map(c, 1, 0, 16, 0x36, &id) != PM_OK) return 2;
    mode = 3; int rc = pm_unmap(c, 1, id);
    if (rc == PM_GL_ERROR && pm_error(c) == 0x502 &&
        pm_unmap(c, 1, id) != PM_OK) return 0;
    fprintf(stderr, "FAIL unmap error lost or incorrectly reported success\n"); return 1;
  }
  if (!strcmp(argv[1], "pointer_reuse")) {
    if (pm_map(c, 1, 0, 16, 0x36, &id) != PM_OK)
      return 2; /* External delete + distinct new object reuses address: driver
                   pointer unchanged. */
    char data = 42;
    int rc = pm_flush(c, 1, id, 0, &data, 1);
    if (rc != PM_OK && !flushes)
      return 0;
    fprintf(stderr, "FAIL stale ID accepted reused pointer; rc=%d flushes=%d\n",
            rc, flushes);
    return 1;
  }
  if (!strcmp(argv[1], "disconnect")) {
    if (pm_map(c, 1, 0, 16, 0x36, &id) != PM_OK)
      return 2;
    pm_ctx_free(c);
    int rc = pm_free(p);
    if (rc == PM_OK && unmaps == 1)
      return 0;
    fprintf(
        stderr,
        "FAIL disconnected group still owns mapped object; rc=%d unmaps=%d\n",
        rc, unmaps);
    return 1;
  }
  return 2;
}
