#ifndef PM_H
#define PM_H
#include <stddef.h>
#include <stdint.h>
struct pm;
struct pm_ctx;
enum {
  PM_OK = 0,
  PM_GL_ERROR = 1,
  PM_INVALID = 2,
  PM_RESOURCE = 3,
  PM_UNSUPPORTED = 4,
  PM_PROTOCOL = 5
};
typedef unsigned (*pm_get_error_fn)(void);
typedef void *(*pm_map_fn)(unsigned, intptr_t, intptr_t, unsigned);
typedef void (*pm_pointer_fn)(unsigned, unsigned, void **);
typedef void (*pm_flush_fn)(unsigned, intptr_t, intptr_t);
typedef unsigned char (*pm_unmap_fn)(unsigned);
struct pm *pm_new(pm_get_error_fn, pm_map_fn, pm_pointer_fn, pm_flush_fn,
                  pm_unmap_fn);
struct pm_ctx *pm_ctx_new(struct pm *, uint64_t);
void pm_ctx_free(struct pm_ctx *);
/* Caller must first confirm the backend actually destroyed the final EGL
 * context of this share group. Reject if any registered context remains. */
int pm_group_retire(struct pm *, uint64_t group);
/* Trusted decoder event only: call after successful backend deletion of the
 * exact object generation represented by id, under the outer decoder lock. */
int pm_forget_after_backend_delete(struct pm_ctx *, uint64_t id);
int pm_free(struct pm *);
unsigned pm_error(struct pm_ctx *);
int pm_map(struct pm_ctx *, unsigned, intptr_t, intptr_t, unsigned, uint64_t *);
int pm_flush(struct pm_ctx *, unsigned, uint64_t, intptr_t, const void *,
             intptr_t);
int pm_unmap(struct pm_ctx *, unsigned, uint64_t);
#endif
