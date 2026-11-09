#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test-only prototype; each child is created by this test, never a real VM. */
extern int test_reap_owned_children(void);

static int supervised_case(void) {
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR || prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) return 2;
    int report[2], resume[2];
    if (pipe(report) != 0 || pipe(resume) != 0) return 2;
    pid_t middle = fork();
    if (middle < 0) return 2;
    if (middle == 0) {
        alarm(10);
        close(report[0]); close(resume[1]);
        pid_t leaf = fork();
        if (leaf < 0) _exit(2);
        if (leaf == 0) {
            alarm(10);
            char go;
            if (read(resume[0], &go, 1) != 1) _exit(2);
            close(resume[0]);
            if (setsid() < 0 || clearenv() != 0) _exit(2);
            if (write(report[1], "R", 1) != 1) _exit(2);
            close(report[1]);
            for (;;) pause();
        }
        close(resume[0]);
        if (write(report[1], &leaf, sizeof leaf) != sizeof leaf) _exit(2);
        close(report[1]);
        _exit(0);
    }
    close(report[1]); close(resume[0]);
    pid_t leaf = -1; int status, ok = read(report[0], &leaf, sizeof leaf) == sizeof leaf && leaf > 0;
    if (waitpid(middle, &status, 0) != middle || !WIFEXITED(status) || WEXITSTATUS(status)) ok = 0;
    int leaf_fd = ok ? (int)syscall(__NR_pidfd_open, leaf, 0) : -1;
    if (leaf_fd < 0) ok = 0;
    if (ok && write(resume[1], "G", 1) != 1) ok = 0;
    close(resume[1]);
    char ready = 0;
    if (ok && (read(report[0], &ready, 1) != 1 || ready != 'R')) ok = 0;
    close(report[0]);
    int result = ok ? test_reap_owned_children() : -1;
    if (result != 0 && leaf > 0) {
        /* The pidfd, not a potentially reused numerical PID, identifies this child. */
        if (leaf_fd >= 0) syscall(__NR_pidfd_send_signal, leaf_fd, SIGKILL, NULL, 0);
        while (waitpid(leaf, &status, 0) < 0 && errno == EINTR) { }
    }
    if (leaf_fd >= 0) close(leaf_fd);
    int empty = waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD;
    return ok && result == 0 && empty ? 0 : 1;
}

int main(void) {
    int ready[2]; if (pipe(ready) != 0) return 2;
    pid_t witness = fork();
    if (witness < 0) return 2;
    if (witness == 0) {
        close(ready[0]); alarm(15);
        if (write(ready[1], "R", 1) != 1) _exit(2);
        close(ready[1]);
        for (;;) pause();
    }
    close(ready[1]); char c = 0;
    int ok = read(ready[0], &c, 1) == 1 && c == 'R'; close(ready[0]);
    int witness_fd = (int)syscall(__NR_pidfd_open, witness, 0);
    if (witness_fd < 0) ok = 0;
    pid_t supervisor = ok ? fork() : -1;
    if (supervisor == 0) _exit(supervised_case());
    if (supervisor < 0) ok = 0;
    int status;
    if (ok && (waitpid(supervisor, &status, 0) != supervisor || !WIFEXITED(status) || WEXITSTATUS(status))) ok = 0;
    int witness_intact = witness_fd >= 0 && syscall(__NR_pidfd_send_signal, witness_fd, 0, NULL, 0) == 0;
    if (witness_fd >= 0) {
        syscall(__NR_pidfd_send_signal, witness_fd, SIGKILL, NULL, 0);
        close(witness_fd);
    }
    while (waitpid(witness, &status, 0) < 0 && errno == EINTR) { }
    printf("TEST_UID=%ld DETACHED_REAPED=%d OTHER_SAME_UID_INTACT=%d\n", (long)getuid(), ok, witness_intact);
    return ok && witness_intact ? 0 : 1;
}
