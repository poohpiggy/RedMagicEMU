#define _GNU_SOURCE
#include "../ringtone_init_quiescence.h"
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line=%d\n", __LINE__); return 1; } } while (0)

static int read_one(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t got = read(fd, (char *)buf + off, n - off);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        off += (size_t)got;
    }
    return 0;
}

static int wait_child(pid_t pid, int flags) {
    int status;
    pid_t got;
    do { got = waitpid(pid, &status, flags); } while (got < 0 && errno == EINTR);
    return got == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int empty_case(void) {
    CHECK(rme_init_children_now() == RME_INIT_EMPTY);
    CHECK(rme_init_subreaper_begin() == RME_INIT_EMPTY);
    int enabled = 0;
    CHECK(prctl(PR_GET_CHILD_SUBREAPER, &enabled) == 0 && enabled == 1);
    CHECK(rme_init_await_empty(100) == RME_INIT_EMPTY);
    CHECK(rme_init_subreaper_begin() != RME_INIT_EMPTY);
    CHECK(rme_init_await_empty(600001) == RME_INIT_ERROR);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) _exit(rme_init_await_empty(0) == RME_INIT_ERROR ? 0 : 1);
    CHECK(wait_child(child, 0));
    return 0;
}

static int existing_child_case(void) {
    int gate[2]; CHECK(pipe(gate) == 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        close(gate[1]); alarm(5);
        char c; int ok = read_one(gate[0], &c, 1) == 0 && c == 'x';
        close(gate[0]); _exit(ok ? 0 : 1);
    }
    close(gate[0]);
    int prior = -1;
    CHECK(prctl(PR_GET_CHILD_SUBREAPER, &prior) == 0 && prior == 0);
    CHECK(rme_init_children_now() == RME_INIT_BUSY);
    CHECK(rme_init_subreaper_begin() == RME_INIT_BUSY);
    int after = -1;
    CHECK(prctl(PR_GET_CHILD_SUBREAPER, &after) == 0 && after == 0);
    CHECK(kill(child, 0) == 0);
    CHECK(write(gate[1], "x", 1) == 1); close(gate[1]);
    CHECK(wait_child(child, 0));
    return 0;
}

static int orphan_and_zombie_case(void) {
    CHECK(rme_init_subreaper_begin() == RME_INIT_EMPTY);
    int info[2], gate[2]; CHECK(pipe(info) == 0 && pipe(gate) == 0);
    pid_t parent = fork(); CHECK(parent >= 0);
    if (!parent) {
        close(info[0]); close(gate[1]); alarm(5);
        pid_t orphan = fork();
        if (orphan < 0) _exit(1);
        if (!orphan) {
            close(info[1]); alarm(5);
            char c; int ok = read_one(gate[0], &c, 1) == 0 && c == 'x';
            close(gate[0]); _exit(ok ? 0 : 1);
        }
        close(gate[0]);
        int ok = write(info[1], &orphan, sizeof orphan) == sizeof orphan;
        close(info[1]); _exit(ok ? 0 : 1);
    }
    close(info[1]); close(gate[0]);
    pid_t orphan = -1; CHECK(read_one(info[0], &orphan, sizeof orphan) == 0); close(info[0]);
    CHECK(orphan > 0 && wait_child(parent, 0));
    CHECK(rme_init_await_empty(40) == RME_INIT_TIMEOUT);
    CHECK(kill(orphan, 0) == 0); /* The facility did not signal it. */
    CHECK(write(gate[1], "x", 1) == 1); close(gate[1]);
    siginfo_t info_state = {0};
    CHECK(waitid(P_PID, (id_t)orphan, &info_state, WEXITED | WNOWAIT) == 0 && info_state.si_pid == orphan);
    CHECK(rme_init_await_empty(40) == RME_INIT_TIMEOUT); /* An unreaped child is not empty. */
    CHECK(wait_child(orphan, 0)); /* waitid(WNOWAIT) did not steal the status. */
    CHECK(rme_init_await_empty(100) == RME_INIT_EMPTY);
    return 0;
}

static int cloned_child(void *arg) {
    int fd = *(int *)arg; alarm(5);
    char c; int ok = read_one(fd, &c, 1) == 0 && c == 'x';
    close(fd); _exit(ok ? 0 : 1);
}

static int non_sigchld_clone_case(void) {
    CHECK(rme_init_subreaper_begin() == RME_INIT_EMPTY);
    CHECK(signal(SIGUSR1, SIG_IGN) != SIG_ERR);
    int gate[2]; CHECK(pipe(gate) == 0);
    char *stack = malloc(64 * 1024); CHECK(stack != NULL);
    pid_t child = clone(cloned_child, stack + 64 * 1024, SIGUSR1, &gate[0]);
    CHECK(child > 0); close(gate[0]);
    CHECK(rme_init_children_now() == RME_INIT_BUSY); /* Includes __WCLONE children. */
    CHECK(kill(child, 0) == 0);
    CHECK(write(gate[1], "x", 1) == 1); close(gate[1]);
    CHECK(wait_child(child, __WALL)); free(stack);
    CHECK(rme_init_await_empty(100) == RME_INIT_EMPTY);
    return 0;
}

static int run_case(const char *name, int (*test)(void)) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) { alarm(8); _exit(test()); }
    int ok = wait_child(child, 0);
    printf("%s=%s\n", name, ok ? "PASS" : "FAIL");
    return !ok;
}

int main(void) {
    int failed = 0;
    failed |= run_case("EMPTY_AND_ENABLE", empty_case);
    failed |= run_case("EXISTING_CHILD_PRESERVED", existing_child_case);
    failed |= run_case("ORPHAN_AND_ZOMBIE_NOT_REAPED", orphan_and_zombie_case);
    failed |= run_case("NON_SIGCHLD_CLONE", non_sigchld_clone_case);
    return failed ? 1 : 0;
}
