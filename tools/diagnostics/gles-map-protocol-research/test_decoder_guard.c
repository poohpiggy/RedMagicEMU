/* Contract test only. Existing test_pm_safety remains unchanged. */
#include "guard.h"
#include "pm.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TARGET 0x8f37u
static pthread_mutex_t event_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cv = PTHREAD_COND_INITIALIZER;
static unsigned char backing[64];
static unsigned char *storage;
static int pointer_ready, allow_flush, flush_done, delete_started, delete_done,
    deletion_fail;
static unsigned get_error(void) { return 0; }
static void *map(unsigned target, intptr_t off, intptr_t len, unsigned flags) {
  (void)target;
  (void)off;
  (void)len;
  (void)flags;
  return storage;
}
static void pointer(unsigned target, unsigned name, void **out) {
  (void)target;
  (void)name;
  *out = storage;
  pthread_mutex_lock(&event_mu);
  pointer_ready = 1;
  pthread_cond_broadcast(&event_cv);
  while (!allow_flush)
    pthread_cond_wait(&event_cv, &event_mu);
  pthread_mutex_unlock(&event_mu);
}
static void flush(unsigned target, intptr_t off, intptr_t len) {
  (void)target;
  (void)off;
  (void)len;
  pthread_mutex_lock(&event_mu);
  flush_done = 1;
  pthread_cond_broadcast(&event_cv);
  pthread_mutex_unlock(&event_mu);
}
static unsigned char unmap(unsigned target) {
  (void)target;
  return 1;
}
static int delete_backend(void *unused) {
  (void)unused;
  pthread_mutex_lock(&event_mu);
  if (deletion_fail) {
    pthread_mutex_unlock(&event_mu);
    return PM_GL_ERROR;
  }
  assert(flush_done);
  storage = NULL;
  delete_done = 1;
  pthread_cond_broadcast(&event_cv);
  pthread_mutex_unlock(&event_mu);
  return PM_OK;
}
struct args {
  struct guard *guard;
  struct pm_ctx *ctx;
  uint64_t id;
  int status;
};
static void *flush_thread(void *v) {
  struct args *a = v;
  a->status = guard_flush(a->guard, a->ctx, TARGET, a->id, 0, "DATA", 4);
  return NULL;
}
static void *delete_thread(void *v) {
  struct args *a = v;
  pthread_mutex_lock(&event_mu);
  delete_started = 1;
  pthread_cond_broadcast(&event_cv);
  pthread_mutex_unlock(&event_mu);
  a->status = guard_delete(a->guard, a->ctx, a->id, delete_backend, NULL);
  return NULL;
}
int main(void) {
  storage = backing;
  struct pm *p = pm_new(get_error, map, pointer, flush, unmap);
  assert(p);
  struct pm_ctx *ctx = pm_ctx_new(p, 1);
  assert(ctx);
  struct guard guard;
  assert(guard_init(&guard) == PM_OK);
  uint64_t id = 0;
  assert(guard_map(&guard, ctx, TARGET, 0, 64, 0x36, &id) == PM_OK);
  struct args f = {&guard, ctx, id, -1}, d = {&guard, ctx, id, -1};
  pthread_t ft, dt;
  assert(!pthread_create(&ft, NULL, flush_thread, &f));
  pthread_mutex_lock(&event_mu);
  while (!pointer_ready)
    pthread_cond_wait(&event_cv, &event_mu);
  pthread_mutex_unlock(&event_mu);
  /* The same outer lock must cover pointer lookup, memcpy, flush and delete. */
  assert(pthread_mutex_trylock(&guard.mutex) == EBUSY);
  assert(!pthread_create(&dt, NULL, delete_thread, &d));
  pthread_mutex_lock(&event_mu);
  while (!delete_started)
    pthread_cond_wait(&event_cv, &event_mu);
  assert(!delete_done);
  allow_flush = 1;
  pthread_cond_broadcast(&event_cv);
  pthread_mutex_unlock(&event_mu);
  pthread_join(ft, NULL);
  pthread_join(dt, NULL);
  assert(f.status == PM_OK && d.status == PM_OK && delete_done);
  /* A stale mapping ID remains invalid even if a new object gets the same
   * storage address. */
  storage = backing;
  uint64_t replacement = 0;
  assert(guard_map(&guard, ctx, TARGET, 0, 64, 0x36, &replacement) == PM_OK &&
         replacement != id);
  assert(guard_flush(&guard, ctx, TARGET, id, 0, "BAD!", 4) != PM_OK);
  /* A failed backend deletion must not invalidate the still-live mapping. */
  deletion_fail = 1;
  assert(guard_delete(&guard, ctx, replacement, delete_backend, NULL) ==
         PM_GL_ERROR);
  assert(guard_flush(&guard, ctx, TARGET, replacement, 0, "LIVE", 4) == PM_OK);
  assert(guard_unmap(&guard, ctx, TARGET, replacement) == PM_OK);
  storage = NULL;
  guard_destroy(&guard);
  pm_ctx_free(ctx);
  assert(pm_free(p) == PM_OK);
  puts("DECODER_GUARD_CONTRACT_OK");
  return 0;
}
