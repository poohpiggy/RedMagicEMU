/* Safety test for an explicit, backend-confirmed last-context retirement. */
#include "pm.h"
#include <assert.h>
#include <stdio.h>
static unsigned char a[16], b[16];
static unsigned err(void) { return 0; }
static void *map(unsigned t, intptr_t o, intptr_t n, unsigned flags) {
  (void)o; (void)n; (void)flags; return t == 1 ? a : b;
}
static void pointer(unsigned t, unsigned pname, void **out) {
  (void)pname; *out = t == 1 ? a : b;
}
static void flush(unsigned t, intptr_t o, intptr_t n) { (void)t; (void)o; (void)n; }
static unsigned char unmap(unsigned t) { (void)t; return 1; }
int main(void) {
  struct pm *p = pm_new(err, map, pointer, flush, unmap); assert(p);
  struct pm_ctx *first = pm_ctx_new(p, 17), *shared = pm_ctx_new(p, 17);
  struct pm_ctx *other = pm_ctx_new(p, 23); assert(first && shared && other);
  uint64_t one = 0, two = 0;
  assert(pm_map(first, 1, 0, 8, 0x36, &one) == PM_OK);
  assert(pm_map(other, 2, 0, 8, 0x36, &two) == PM_OK);
  assert(pm_group_retire(p, 17) == PM_INVALID);
  pm_ctx_free(first);
  assert(pm_group_retire(p, 17) == PM_INVALID);
  const char byte = 42; assert(pm_flush(shared, 1, one, 0, &byte, 1) == PM_OK);
  pm_ctx_free(shared);
  /* Simulated backend has now destroyed the last real EGL context in group 17. */
  assert(pm_group_retire(p, 17) == PM_OK);
  assert(pm_ctx_new(p, 17) == NULL); /* no old-group identity reuse */
  assert(pm_flush(other, 2, two, 0, &byte, 1) == PM_OK); /* other group unaffected */
  pm_ctx_free(other);
  assert(pm_free(p) == PM_INVALID);
  /* Simulated backend has now destroyed the last real EGL context in group 23. */
  assert(pm_group_retire(p, 23) == PM_OK);
  assert(pm_group_retire(p, 23) == PM_INVALID);
  assert(pm_free(p) == PM_OK);
  puts("GROUP_RETIRE_EXPLICIT_BACKEND_TEST_OK");
  return 0;
}
