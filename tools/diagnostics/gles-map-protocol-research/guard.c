/* Experimental outer decoder lock; not connected to production host.c. */
#include "guard.h"
int guard_init(struct guard *g) {
  return !g || pthread_mutex_init(&g->mutex, NULL) ? PM_RESOURCE : PM_OK;
}
void guard_destroy(struct guard *g) {
  if (g)
    pthread_mutex_destroy(&g->mutex);
}
int guard_map(struct guard *g, struct pm_ctx *c, unsigned target, intptr_t off,
              intptr_t len, unsigned flags, uint64_t *id) {
  if (!g)
    return PM_INVALID;
  pthread_mutex_lock(&g->mutex);
  int rc = pm_map(c, target, off, len, flags, id);
  pthread_mutex_unlock(&g->mutex);
  return rc;
}
int guard_flush(struct guard *g, struct pm_ctx *c, unsigned target, uint64_t id,
                intptr_t off, const void *data, intptr_t len) {
  if (!g)
    return PM_INVALID;
  pthread_mutex_lock(&g->mutex);
  int rc = pm_flush(c, target, id, off, data, len);
  pthread_mutex_unlock(&g->mutex);
  return rc;
}
int guard_unmap(struct guard *g, struct pm_ctx *c, unsigned target,
                uint64_t id) {
  if (!g)
    return PM_INVALID;
  pthread_mutex_lock(&g->mutex);
  int rc = pm_unmap(c, target, id);
  pthread_mutex_unlock(&g->mutex);
  return rc;
}
int guard_delete(struct guard *g, struct pm_ctx *c, uint64_t id,
                 int (*backend_delete)(void *), void *arg) {
  if (!g || !backend_delete)
    return PM_INVALID;
  pthread_mutex_lock(&g->mutex);
  int rc = backend_delete(arg);
  if (rc == PM_OK && pm_forget_after_backend_delete(c, id) != PM_OK)
    rc = PM_PROTOCOL;
  pthread_mutex_unlock(&g->mutex);
  return rc;
}
