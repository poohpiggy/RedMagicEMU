#define _GNU_SOURCE
#include "../vibration_binder_handle.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

static void *waiter(void *arg) {
    uint64_t id = *(uint64_t *)arg;
    struct rme_vib_step_event event;
    assert(rme_vib_handle_next(id, &event) == RME_VIB_STEP_STOPPED);
    return NULL;
}
int main(void) {
    int s[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, s) == 0);
    int pidfd = (int)syscall(SYS_pidfd_open, getpid(), 0); assert(pidfd >= 0);
    const unsigned char header[12] = {0,0,0,0,0,0,0,0,'T','S','Y','S'};
    uint64_t h = rme_vib_handle_open(s[0], pidfd, getpid(), getuid(), header);
    assert(h != 0); close(s[0]); close(pidfd);
    char packet[4096];
    assert(recv(s[1], packet, sizeof packet, 0) > 0); /* hello */
    assert(recv(s[1], packet, sizeof packet, 0) > 0); /* enter */
    assert(recv(s[1], packet, sizeof packet, 0) > 0); /* registration */
    pthread_t t; assert(!pthread_create(&t, NULL, waiter, &h));
    assert(recv(s[1], packet, sizeof packet, 0) > 0); /* blocked OP_READ */
    assert(!rme_vib_handle_destroy(h));
    assert(!pthread_join(t, NULL));
    struct rme_vib_step_event e;
    assert(rme_vib_handle_next(h, &e) < 0);
    assert(rme_vib_handle_complete(h, 1, 1) < 0);
    assert(rme_vib_handle_destroy(h) < 0);
    close(s[1]);
    puts("VIBRATION_BINDER_HANDLE_OK");
}
