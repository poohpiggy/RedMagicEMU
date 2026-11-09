#define _GNU_SOURCE
#include "vibration_binder_handle.h"
#include "vibration_binder_auth.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

struct handle { struct rme_binder_auth auth; struct rme_bproto_client client; struct rme_vib_step step; uint64_t id; unsigned refs; };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t idle = PTHREAD_COND_INITIALIZER;
static struct handle *slots[16];
static uint64_t serial;

static int alive(void *p) { return rme_binder_auth_alive(p); }
static struct handle *acquire(uint64_t id) {
    struct handle *h = NULL;
    pthread_mutex_lock(&lock);
    for (unsigned i=0; id && i<16; ++i) if (slots[i] && slots[i]->id == id) { h=slots[i]; h->refs++; break; }
    pthread_mutex_unlock(&lock);
    if (!h) errno = EBADF;
    return h;
}
static void release(struct handle *h) {
    pthread_mutex_lock(&lock); --h->refs; pthread_cond_broadcast(&idle); pthread_mutex_unlock(&lock);
}
static int random_nonzero(void *p, size_t n) {
    unsigned char *b=p; size_t done=0;
    while (done<n) { ssize_t k=getrandom(b+done,n-done,0); if (k<0 && errno==EINTR) continue; if (k<=0) return -1; done+=(size_t)k; }
    unsigned char any=0; for (size_t i=0;i<n;++i) any|=b[i];
    return any ? 0 : -1;
}
uint64_t rme_vib_handle_open(int socket_fd, int pidfd, pid_t pid, uid_t uid, const unsigned char header[12]) {
    if (!header || memcmp(header+8,"TSYS",4)) { errno=EINVAL; return 0; }
    struct handle *h=calloc(1,sizeof *h); if (!h) return 0;
    h->auth.socket_fd=h->auth.pidfd=h->client.fd=-1;
    if (rme_binder_auth_open(&h->auth,socket_fd,pidfd,pid,uid)) goto fail;
    int sock=fcntl(h->auth.socket_fd,F_DUPFD_CLOEXEC,0); if (sock<0) goto fail;
    uint32_t key=0; uint64_t ptr=0,cookie=0;
    if (random_nonzero(&key,sizeof key) || random_nonzero(&ptr,sizeof ptr) || random_nonzero(&cookie,sizeof cookie)) { close(sock); goto fail; }
    if (rme_bproto_open(&h->client,sock,pid,uid,1000,key)) goto fail;
    struct rme_vib_add_service_input in={.binder_flags=0x113,.binder_ptr=ptr,.binder_cookie=cookie,.stability=63};
    memcpy(in.interface_header,header,12);
    if (rme_vib_step_init(&h->step,&h->client,&in,alive,&h->auth)) goto fail;
    pthread_mutex_lock(&lock);
    unsigned i=0; while (i<16 && slots[i]) ++i;
    if (i==16 || serial==UINT64_MAX) { pthread_mutex_unlock(&lock); errno=ENOSPC; goto fail_step; }
    h->id=++serial; slots[i]=h;
    pthread_mutex_unlock(&lock); return h->id;
fail_step: rme_vib_step_destroy(&h->step);
fail: { int e=errno; rme_bproto_close(&h->client); rme_binder_auth_close(&h->auth); free(h); errno=e; return 0; }
}
int rme_vib_handle_next(uint64_t id, struct rme_vib_step_event *event) {
    struct handle *h=acquire(id); if (!h) return -1; int r=rme_vib_step_next(&h->step,event); release(h); return r;
}
int rme_vib_handle_complete(uint64_t id, uint64_t token, int ok) {
    struct handle *h=acquire(id); if (!h) return -1; int r=rme_vib_step_complete(&h->step,token,ok); release(h); return r;
}
int rme_vib_handle_stop(uint64_t id) {
    struct handle *h=acquire(id); if (!h) return -1; int r=rme_vib_step_stop(&h->step); release(h); return r;
}
int rme_vib_handle_destroy(uint64_t id) {
    struct handle *h=NULL; pthread_mutex_lock(&lock);
    for (unsigned i=0;id && i<16;++i) if (slots[i] && slots[i]->id==id) { h=slots[i]; slots[i]=NULL; break; }
    pthread_mutex_unlock(&lock); if (!h) { errno=EBADF; return -1; }
    int rc=rme_vib_step_stop(&h->step);
    pthread_mutex_lock(&lock); while (h->refs) pthread_cond_wait(&idle,&lock); pthread_mutex_unlock(&lock);
    rme_vib_step_destroy(&h->step); rme_binder_auth_close(&h->auth); free(h); return rc;
}
