#ifndef GUARD_H
#define GUARD_H
#include "pm.h"
#include <pthread.h>
/* Research contract: every relevant backend operation must use this mutex. */
struct guard {
  pthread_mutex_t mutex;
};
int guard_init(struct guard *);
void guard_destroy(struct guard *);
int guard_map(struct guard *, struct pm_ctx *, unsigned, intptr_t, intptr_t,
              unsigned, uint64_t *);
int guard_flush(struct guard *, struct pm_ctx *, unsigned, uint64_t, intptr_t,
                const void *, intptr_t);
int guard_unmap(struct guard *, struct pm_ctx *, unsigned, uint64_t);
/* The callback must perform the real backend delete and return PM_OK only if it
 * succeeded. The caller must supply the exact mapping belonging to the deleted
 * object generation. */
int guard_delete(struct guard *, struct pm_ctx *, uint64_t, int (*)(void *),
                 void *);
#endif
