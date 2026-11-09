#define _GNU_SOURCE
#include "../ringtone_init_quiescence.h"
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

enum mode { WAIT_DENIED, SET_DENIED, VERIFY_FAIL, WAIT_INTERRUPTED, SLEEP_INTERRUPTED };
static enum mode test_mode;
static int wait_calls;
static int get_calls;
static int unexpected;

int __wrap_waitid(idtype_t type, id_t id, siginfo_t *info, int options) {
    (void)id; (void)info;
    if (type != P_ALL || options != (WEXITED | WNOHANG | WNOWAIT | __WALL)) unexpected = 1;
    ++wait_calls;
    if (test_mode == WAIT_DENIED) { errno = EPERM; return -1; }
    if (test_mode == WAIT_INTERRUPTED) { errno = EINTR; return -1; }
    if (test_mode == SLEEP_INTERRUPTED && wait_calls > 2) return 0;
    errno = ECHILD;
    return -1;
}

int __wrap_prctl(int option, ...) {
    va_list args;
    va_start(args, option);
    if (option == PR_GET_CHILD_SUBREAPER) {
        int *result = va_arg(args, int *);
        ++get_calls;
        *result = get_calls == 1 || test_mode == VERIFY_FAIL ? 0 : 1;
    } else if (option == PR_SET_CHILD_SUBREAPER) {
        (void)va_arg(args, int);
        if (test_mode == SET_DENIED) { va_end(args); errno = EPERM; return -1; }
    } else {
        unexpected = 1;
        va_end(args);
        errno = EINVAL;
        return -1;
    }
    va_end(args);
    return 0;
}

int __wrap_nanosleep(const struct timespec *duration, struct timespec *remaining) {
    (void)duration; (void)remaining;
    errno = EINTR;
    return -1;
}

static int run(enum mode mode, enum rme_init_quiescence expected) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        test_mode = mode;
        if (rme_init_await_empty(0) != RME_INIT_ERROR) _exit(1);
        enum rme_init_quiescence result = rme_init_subreaper_begin();
        if (mode == SLEEP_INTERRUPTED && result == RME_INIT_EMPTY)
            result = rme_init_await_empty(100);
        _exit(result == expected && !unexpected ? 0 : 1);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 1;
    return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

int main(void) {
    int failed = 0;
    failed |= run(WAIT_DENIED, RME_INIT_UNSUPPORTED);
    failed |= run(SET_DENIED, RME_INIT_UNSUPPORTED);
    failed |= run(VERIFY_FAIL, RME_INIT_ERROR);
    failed |= run(WAIT_INTERRUPTED, RME_INIT_INTERRUPTED);
    failed |= run(SLEEP_INTERRUPTED, RME_INIT_INTERRUPTED);
    puts(failed ? "FAULT_INJECTION=FAIL" : "FAULT_INJECTION=PASS");
    return failed;
}
