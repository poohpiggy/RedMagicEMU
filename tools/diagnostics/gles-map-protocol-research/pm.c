/* Research candidate ONLY. Not wired into RedMagicEMU's production protocol.
 * Callers must serialize EVERY GL buffer mutation with this bridge mutex and
 * ensure the correct EGL context is current for each operation. */
#include "pm.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define BUFFER_MAP_POINTER 0x88BDu
#define CONTEXT_LOST 0x0507u
#define PM_CORRUPT 6
#define MAX_MAPS 128
#define MAX_ERRORS 16
struct entry {
  uint64_t id, group;
  void *host_ptr;
  intptr_t length;
  int poisoned;
};
struct retired_group {
  uint64_t id;
  struct retired_group *next;
};
struct pm {
  pthread_mutex_t mu;
  pm_get_error_fn get_error;
  pm_map_fn map;
  pm_pointer_fn pointer;
  pm_flush_fn flush;
  pm_unmap_fn unmap;
  uint64_t next_id;
  struct entry maps[MAX_MAPS];
  unsigned contexts;
  struct pm_ctx *live_contexts;
  struct retired_group *retired;
  int uncertain_host_mapping;
};
struct pm_ctx {
  struct pm *p;
  struct pm_ctx *next;
  uint64_t group;
  unsigned errors[MAX_ERRORS];
  unsigned nerrors;
  int poisoned;
};
struct pm *pm_new(pm_get_error_fn get, pm_map_fn map, pm_pointer_fn pointer,
                  pm_flush_fn flush, pm_unmap_fn unmap) {
  if (!get || !map || !pointer || !flush || !unmap)
    return NULL;
  struct pm *p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  if (pthread_mutex_init(&p->mu, NULL)) {
    free(p);
    return NULL;
  }
  p->get_error = get;
  p->map = map;
  p->pointer = pointer;
  p->flush = flush;
  p->unmap = unmap;
  p->next_id = 1;
  return p;
}
struct pm_ctx *pm_ctx_new(struct pm *p, uint64_t group) {
  if (!p || !group)
    return NULL;
  struct pm_ctx *c = calloc(1, sizeof(*c));
  if (!c)
    return NULL;
  c->p = p;
  c->group = group;
  pthread_mutex_lock(&p->mu);
  for (struct retired_group *r = p->retired; r; r = r->next) {
    if (r->id == group) {
      pthread_mutex_unlock(&p->mu);
      free(c);
      return NULL;
    }
  }
  c->next = p->live_contexts;
  p->live_contexts = c;
  ++p->contexts;
  pthread_mutex_unlock(&p->mu);
  return c;
}
void pm_ctx_free(struct pm_ctx *c) {
  if (!c)
    return;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  struct pm_ctx **link = &p->live_contexts;
  while (*link && *link != c)
    link = &(*link)->next;
  if (*link) {
    *link = c->next;
    --p->contexts;
  }
  pthread_mutex_unlock(&p->mu);
  free(c);
}
/* The caller is trusted to invoke this only after actual backend destruction.
 * It retires bookkeeping, not GL resources, and does not touch stale pointers.
 */
int pm_group_retire(struct pm *p, uint64_t group) {
  if (!p || !group)
    return PM_INVALID;
  pthread_mutex_lock(&p->mu);
  for (struct pm_ctx *c = p->live_contexts; c; c = c->next)
    if (c->group == group) {
      pthread_mutex_unlock(&p->mu);
      return PM_INVALID;
    }
  for (struct retired_group *r = p->retired; r; r = r->next)
    if (r->id == group) {
      pthread_mutex_unlock(&p->mu);
      return PM_INVALID;
    }
  struct retired_group *retired = malloc(sizeof(*retired));
  if (!retired) {
    pthread_mutex_unlock(&p->mu);
    return PM_RESOURCE;
  }
  retired->id = group;
  retired->next = p->retired;
  p->retired = retired;
  for (int i = 0; i < MAX_MAPS; i++)
    if (p->maps[i].id && p->maps[i].group == group)
      memset(&p->maps[i], 0, sizeof(p->maps[i]));
  pthread_mutex_unlock(&p->mu);
  return PM_OK;
}
int pm_free(struct pm *p) {
  if (!p)
    return PM_INVALID;
  pthread_mutex_lock(&p->mu);
  int busy = p->contexts != 0 || p->uncertain_host_mapping;
  for (int i = 0; i < MAX_MAPS; i++)
    busy |= p->maps[i].id != 0;
  pthread_mutex_unlock(&p->mu);
  if (busy)
    return PM_INVALID;
  struct retired_group *r = p->retired;
  while (r) {
    struct retired_group *next = r->next;
    free(r);
    r = next;
  }
  pthread_mutex_destroy(&p->mu);
  free(p);
  return PM_OK;
}
/* Preserve each distinct error flag. Any overflow/non-terminating drain poisons
 * this context and prevents all later calls from being reported successful. */
static int collect(struct pm_ctx *c) {
  int observed = 0;
  for (int i = 0; i < 32; i++) {
    unsigned e = c->p->get_error();
    if (!e)
      return observed;
    observed++;
    unsigned j = 0;
    while (j < c->nerrors && c->errors[j] != e)
      j++;
    if (j == c->nerrors) {
      if (c->nerrors == MAX_ERRORS) {
        c->poisoned = 1;
        return -1;
      }
      c->errors[c->nerrors++] = e;
    }
    if (e == CONTEXT_LOST)
      c->poisoned = 1;
  }
  c->poisoned = 1;
  return -1;
}
unsigned pm_error(struct pm_ctx *c) {
  if (!c)
    return 0;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  if (!c->nerrors)
    collect(c);
  unsigned e = 0;
  if (c->nerrors) {
    e = c->errors[0];
    memmove(c->errors, c->errors + 1, (--c->nerrors) * sizeof(unsigned));
  }
  pthread_mutex_unlock(&p->mu);
  return e;
}
static struct entry *find(struct pm_ctx *c, uint64_t id) {
  for (int i = 0; i < MAX_MAPS; i++)
    if (c->p->maps[i].id == id && c->p->maps[i].group == c->group)
      return &c->p->maps[i];
  return NULL;
}
int pm_forget_after_backend_delete(struct pm_ctx *c, uint64_t id) {
  if (!c || !id)
    return PM_INVALID;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  struct entry *m = find(c, id);
  if (!m) {
    pthread_mutex_unlock(&p->mu);
    return PM_INVALID;
  }
  memset(m, 0, sizeof(*m));
  pthread_mutex_unlock(&p->mu);
  return PM_OK;
}
static int pre(struct pm_ctx *c) {
  if (c->poisoned)
    return PM_PROTOCOL;
  if (collect(c) < 0 || c->poisoned)
    return PM_PROTOCOL;
  return PM_OK;
}
static int verify_bound(struct pm_ctx *c, unsigned target, struct entry *m) {
  void *ptr = NULL;
  c->p->pointer(target, BUFFER_MAP_POINTER, &ptr);
  int n = collect(c);
  if (n < 0 || c->poisoned)
    return PM_PROTOCOL;
  if (n)
    return PM_GL_ERROR;
  if (ptr != m->host_ptr)
    return PM_INVALID;
  return PM_OK;
}
int pm_map(struct pm_ctx *c, unsigned target, intptr_t offset, intptr_t length,
           unsigned access, uint64_t *id) {
  if (!c || !id)
    return PM_INVALID;
  *id = 0;
  if (access != 0x36u)
    return PM_UNSUPPORTED;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  int rc = pre(c);
  if (rc)
    goto done;
  struct entry *slot = NULL;
  for (int i = 0; i < MAX_MAPS; i++)
    if (!p->maps[i].id) {
      slot = &p->maps[i];
      break;
    }
  if (!slot || !p->next_id) {
    rc = PM_RESOURCE;
    goto done;
  }
  void *ptr = p->map(target, offset, length, access);
  int err = collect(c);
  if (err < 0 || c->poisoned) {
    if (ptr)
      p->uncertain_host_mapping = 1;
    rc = PM_PROTOCOL;
    goto done;
  }
  if (err || !ptr) {
    if (ptr) {
      /* A broken/contradictory backend returned both a pointer and an error.
       * Roll back while the same context and binding are current; preserve all
       * error flags and quarantine uncertain cleanup rather than report OK. */
      unsigned char undone = p->unmap(target);
      int undo_error = collect(c);
      if (!undone || undo_error || c->poisoned) {
        p->uncertain_host_mapping = 1;
        c->poisoned = 1;
        rc = PM_PROTOCOL;
        goto done;
      }
    }
    rc = err ? PM_GL_ERROR : PM_PROTOCOL;
    goto done;
  }
  /* Duplicate pointers in a share group are ambiguous even if rare. Roll back
   * the just-created host mapping; do not claim success or guess identity. */
  for (int i = 0; i < MAX_MAPS; i++)
    if (p->maps[i].id && p->maps[i].group == c->group &&
        p->maps[i].host_ptr == ptr) {
      unsigned char ok = p->unmap(target);
      int e = collect(c);
      if (!ok || e || c->poisoned) {
        c->poisoned = 1;
        p->uncertain_host_mapping = 1;
      }
      rc = PM_PROTOCOL;
      goto done;
    }
  slot->id = p->next_id++;
  slot->group = c->group;
  slot->host_ptr = ptr;
  slot->length = length;
  slot->poisoned = 0;
  *id = slot->id;
  rc = PM_OK;
done:
  pthread_mutex_unlock(&p->mu);
  return rc;
}
int pm_flush(struct pm_ctx *c, unsigned target, uint64_t id, intptr_t offset,
             const void *data, intptr_t length) {
  if (!c || (!data && length))
    return PM_INVALID;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  int rc = pre(c);
  if (rc)
    goto done;
  struct entry *m = find(c, id);
  if (!m || m->poisoned || offset < 0 || length < 0 || offset > m->length ||
      length > m->length - offset) {
    rc = PM_INVALID;
    goto done;
  }
  rc = verify_bound(c, target, m);
  if (rc)
    goto done;
  if (length)
    memcpy((char *)m->host_ptr + offset, data, (size_t)length);
  p->flush(target, offset, length);
  int err = collect(c);
  if (err || c->poisoned) {
    m->poisoned = 1;
    rc = err < 0 || c->poisoned ? PM_PROTOCOL : PM_GL_ERROR;
    goto done;
  }
  rc = PM_OK;
done:
  pthread_mutex_unlock(&p->mu);
  return rc;
}
int pm_unmap(struct pm_ctx *c, unsigned target, uint64_t id) {
  if (!c)
    return PM_INVALID;
  struct pm *p = c->p;
  pthread_mutex_lock(&p->mu);
  int rc = pre(c);
  if (rc)
    goto done;
  struct entry *m = find(c, id);
  if (!m || m->poisoned) {
    rc = PM_INVALID;
    goto done;
  }
  rc = verify_bound(c, target, m);
  if (rc)
    goto done;
  unsigned char ok = p->unmap(target);
  int err = collect(c);
  if (err || c->poisoned) {
    m->poisoned = 1;
    rc = err < 0 || c->poisoned ? PM_PROTOCOL : PM_GL_ERROR;
    goto done;
  }
  memset(m, 0, sizeof(*m));
  rc = ok ? PM_OK : PM_CORRUPT;
done:
  pthread_mutex_unlock(&p->mu);
  return rc;
}
