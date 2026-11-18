/* Standalone socket mirror experiment. Not integrated into EMU; no ABI
 * promises. */
#include "gm.h"
#include "pm.h"
#include "wire.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define GM_MAX 128
#define GM_MAX_BYTES (64u * 1024u * 1024u)
struct local_map {
  uint64_t id;
  void *p;
  intptr_t length;
};
struct gm {
  int fd;
  int poisoned;
  struct local_map maps[GM_MAX];
};
static int send_all(int fd, const void *p, size_t n) {
  const char *q = p;
  while (n) {
    ssize_t k = send(fd, q, n, MSG_NOSIGNAL);
    if (k < 0 && errno == EINTR)
      continue;
    if (k <= 0)
      return -1;
    q += k;
    n -= (size_t)k;
  }
  return 0;
}
static int recv_all(int fd, void *p, size_t n) {
  char *q = p;
  while (n) {
    ssize_t k = recv(fd, q, n, 0);
    if (k < 0 && errno == EINTR)
      continue;
    if (k <= 0)
      return -1;
    q += k;
    n -= (size_t)k;
  }
  return 0;
}
static int rpc(struct gm *g, const struct wire_req *q, const void *data,
               size_t n, uint64_t *value) {
  if (!g || g->poisoned)
    return PM_PROTOCOL;
  struct wire_resp reply;
  if (send_all(g->fd, q, sizeof(*q)) || (n && send_all(g->fd, data, n)) ||
      recv_all(g->fd, &reply, sizeof(reply))) {
    g->poisoned = 1;
    return PM_PROTOCOL;
  }
  if (reply.status < 0 || reply.status > 6) {
    g->poisoned = 1;
    return PM_PROTOCOL;
  }
  if (value)
    *value = reply.value;
  return reply.status;
}
struct gm *gm_new(int fd) {
  if (fd < 0)
    return NULL;
  struct gm *g = calloc(1, sizeof(*g));
  if (g)
    g->fd = fd;
  return g;
}
int gm_free(struct gm *g) {
  if (!g)
    return PM_INVALID;
  for (int i = 0; i < GM_MAX; i++)
    if (g->maps[i].id)
      return PM_INVALID;
  free(g);
  return PM_OK;
}
static struct local_map *find(struct gm *g, uint64_t id) {
  if (!id)
    return NULL;
  for (int i = 0; i < GM_MAX; i++)
    if (g->maps[i].id == id)
      return &g->maps[i];
  return NULL;
}
int gm_map(struct gm *g, unsigned target, intptr_t off, intptr_t n,
           unsigned flags, uint64_t *id, void **ptr) {
  if (!g || !id || !ptr)
    return PM_INVALID;
  *id = 0;
  *ptr = NULL;
  if (flags != 0x36)
    return PM_UNSUPPORTED;
  if (n <= 0 || (uintptr_t)n > GM_MAX_BYTES)
    return PM_RESOURCE;
  struct local_map *m = NULL;
  for (int i = 0; i < GM_MAX; i++)
    if (!g->maps[i].id) {
      m = &g->maps[i];
      break;
    }
  if (!m)
    return PM_RESOURCE;
  void *mirror = malloc((size_t)n);
  if (!mirror)
    return PM_RESOURCE;
  struct wire_req q = {W_MAP, target, 0, off, n, flags};
  uint64_t got = 0;
  int rc = rpc(g, &q, NULL, 0, &got);
  if (rc) {
    free(mirror);
    return rc;
  }
  if (!got || find(g, got)) {
    free(mirror);
    g->poisoned = 1;
    return PM_PROTOCOL;
  }
  m->id = got;
  m->p = mirror;
  m->length = n;
  *id = got;
  *ptr = mirror;
  return PM_OK;
}
int gm_flush(struct gm *g, unsigned target, uint64_t id, intptr_t off,
             intptr_t n) {
  if (!g)
    return PM_INVALID;
  struct local_map *m = find(g, id);
  if (!m || off < 0 || n < 0 || off > m->length || n > m->length - off)
    return PM_INVALID;
  struct wire_req q = {W_FLUSH, target, id, off, n, 0};
  return rpc(g, &q, (char *)m->p + off, (size_t)n, NULL);
}
int gm_unmap(struct gm *g, unsigned target, uint64_t id) {
  if (!g)
    return PM_INVALID;
  struct local_map *m = find(g, id);
  if (!m)
    return PM_INVALID;
  struct wire_req q = {W_UNMAP, target, id, 0, 0, 0};
  int rc = rpc(g, &q, NULL, 0, NULL);
  if (rc == PM_OK || rc == 6) {
    free(m->p);
    memset(m, 0, sizeof(*m));
  }
  return rc;
}
int gm_error(struct gm *g, unsigned *e) {
  if (!g || !e)
    return PM_INVALID;
  struct wire_req q = {W_ERROR, 0, 0, 0, 0, 0};
  uint64_t value = 0;
  int rc = rpc(g, &q, NULL, 0, &value);
  if (!rc)
    *e = (unsigned)value;
  return rc;
}
int gm_bind_test(struct gm *g, unsigned target, unsigned b) {
  struct wire_req q = {W_BIND_TEST, target, 0, 0, 0, b};
  return rpc(g, &q, NULL, 0, NULL);
}
int gm_draw_test(struct gm *g, unsigned a, unsigned b, uint64_t *pixels) {
  if (!pixels)
    return PM_INVALID;
  struct wire_req q = {W_DRAW_TEST, a, 0, 0, 0, b};
  return rpc(g, &q, NULL, 0, pixels);
}
