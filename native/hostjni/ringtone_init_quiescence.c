#define _GNU_SOURCE
#include "ringtone_init_quiescence.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

/* Entry and all creation gates must be serialized by the caller. */
static atomic_int owner_pid;

static enum rme_init_quiescence failed(int error) {
    if (error == EINTR) return RME_INIT_INTERRUPTED;
    if (error == ENOSYS || error == EINVAL || error == EPERM || error == EACCES)
        return RME_INIT_UNSUPPORTED;
    return RME_INIT_ERROR;
}

enum rme_init_quiescence rme_init_children_now(void) {
    siginfo_t info = {0};
    if (waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT | __WALL) == 0)
        return RME_INIT_BUSY;
    return errno == ECHILD ? RME_INIT_EMPTY : failed(errno);
}

enum rme_init_quiescence rme_init_subreaper_begin(void) {
    enum rme_init_quiescence state = rme_init_children_now();
    if (state != RME_INIT_EMPTY) return state;

    int enabled = -1;
    if (prctl(PR_GET_CHILD_SUBREAPER, &enabled, 0, 0, 0) != 0) return failed(errno);
    if (enabled != 0) return RME_INIT_ERROR;
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) return failed(errno);
    enabled = -1;
    if (prctl(PR_GET_CHILD_SUBREAPER, &enabled, 0, 0, 0) != 0) return failed(errno);
    if (enabled != 1) return RME_INIT_ERROR;

    state = rme_init_children_now();
    if (state == RME_INIT_EMPTY) atomic_store(&owner_pid, (int)getpid());
    return state;
}

static int monotonic_ns(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return -1;
    *value = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return 0;
}

enum rme_init_quiescence rme_init_await_empty(unsigned int timeout_ms) {
    if (atomic_load(&owner_pid) != (int)getpid() || timeout_ms > 600000U)
        return RME_INIT_ERROR;
    uint64_t start;
    if (monotonic_ns(&start) != 0) return RME_INIT_ERROR;
    uint64_t deadline = start + (uint64_t)timeout_ms * UINT64_C(1000000);
    if (deadline < start) return RME_INIT_ERROR;

    for (;;) {
        enum rme_init_quiescence state = rme_init_children_now();
        if (state != RME_INIT_BUSY) return state;
        uint64_t now;
        if (monotonic_ns(&now) != 0 || now < start) return RME_INIT_ERROR;
        if (now >= deadline) return RME_INIT_TIMEOUT;
        uint64_t delay = deadline - now;
        if (delay > UINT64_C(5000000)) delay = UINT64_C(5000000);
        struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)delay };
        if (nanosleep(&pause, NULL) != 0) return failed(errno);
    }
}
