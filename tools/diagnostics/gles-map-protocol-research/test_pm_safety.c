/* Fake GL safety regression for the research prototype. */
#include "pm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TARGET 0x8f37u
static unsigned char bytes[64];
static unsigned char *mem = bytes;
static unsigned err;
static int mapped;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int pause_query, query_ready, query_continue;

static unsigned get_error(void) { unsigned e=err; err=0; return e; }
static void *map(unsigned t, intptr_t o, intptr_t n, unsigned a) {
  (void)t; (void)o; (void)n; (void)a;
  if (mapped) return NULL;
  mapped=1; return mem;
}
static void pointer(unsigned t, unsigned pname, void **out) {
  (void)t; (void)pname; *out=mapped ? mem : NULL;
  pthread_mutex_lock(&mu);
  if (pause_query) {
    query_ready=1; pthread_cond_signal(&cv);
    while (!query_continue) pthread_cond_wait(&cv,&mu);
  }
  pthread_mutex_unlock(&mu);
}
static void flush(unsigned t, intptr_t o, intptr_t n) { (void)t; (void)o; (void)n; }
static unsigned char unmap(unsigned t) { (void)t; mapped=0; return 1; }
static struct pm *manager(void) { return pm_new(get_error,map,pointer,flush,unmap); }
static int initial(struct pm_ctx *c, uint64_t *id) { return pm_map(c,TARGET,0,64,0x36,id)==PM_OK && *id; }

/* A is deleted; B is mapped by an uncoordinated host path at the same address. */
static int aba(void) {
  struct pm *p=manager(); struct pm_ctx *c=pm_ctx_new(p,1); uint64_t id=0;
  if (!initial(c,&id)) return 2;
  mapped=0; mapped=1;
  int rc=pm_flush(c,TARGET,id,0,"BAD!",4);
  if (rc==PM_OK && !memcmp(mem,"BAD!",4)) { puts("FAIL ABA: stale A token wrote to B"); return 1; }
  puts("PASS ABA"); return 0;
}
struct args { struct pm_ctx *c; uint64_t id; int rc; };
static void *do_flush(void *v) { struct args *a=v; a->rc=pm_flush(a->c,TARGET,a->id,0,"RACE",4); return NULL; }
/* Mimics another host connection bypassing pm's private mutex. */
static int race(int free_storage) {
  if (free_storage) { mem=malloc(64); if (!mem) return 2; }
  struct pm *p=manager(); struct pm_ctx *c=pm_ctx_new(p,1); uint64_t id=0;
  if (!initial(c,&id)) return 2;
  struct args a={c,id,-1}; pthread_t thread;
  pthread_mutex_lock(&mu); pause_query=1; pthread_mutex_unlock(&mu);
  if (pthread_create(&thread,NULL,do_flush,&a)) return 2;
  pthread_mutex_lock(&mu);
  while (!query_ready) pthread_cond_wait(&cv,&mu);
  if (free_storage) { free(mem); mem=NULL; mapped=0; } else { mapped=0; mapped=1; }
  query_continue=1; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu);
  pthread_join(thread,NULL);
  if (a.rc==PM_OK && (free_storage || !memcmp(bytes,"RACE",4))) {
    puts("FAIL RACE: invalidated mapping accepted after concurrent delete"); return 1;
  }
  puts("PASS RACE"); return 0;
}
static int lifecycle(void) {
  struct pm *p=manager(); struct pm_ctx *c=pm_ctx_new(p,1); uint64_t id=0;
  if (!initial(c,&id)) return 2;
  pm_ctx_free(c);
  if (pm_free(p)!=PM_OK) { puts("FAIL LIFECYCLE: final context leaves mapping slot"); return 1; }
  puts("PASS LIFECYCLE"); return 0;
}
static int errors(void) {
  struct pm *p=manager(); struct pm_ctx *c=pm_ctx_new(p,1); uint64_t id=0;
  err=0x500; if (!initial(c,&id)) return 2;
  /* Models the existing h_api.h direct-driver glGetError dispatch. */
  unsigned direct=get_error(), queued=pm_error(c);
  if (!direct && queued==0x500) { puts("FAIL ERROR_DISPATCH: direct driver read misses queued error"); return 1; }
  if (direct!=0x500) return 2;
  puts("PASS ERROR_DISPATCH"); return 0;
}
int main(int argc, char **argv) {
  if (argc!=2) return 2;
  if (!strcmp(argv[1],"aba")) return aba();
  if (!strcmp(argv[1],"race")) return race(0);
  if (!strcmp(argv[1],"uaf")) return race(1);
  if (!strcmp(argv[1],"lifecycle")) return lifecycle();
  if (!strcmp(argv[1],"errors")) return errors();
  return 2;
}
