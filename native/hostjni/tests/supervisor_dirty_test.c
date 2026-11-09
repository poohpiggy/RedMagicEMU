#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern int test_supervisor_mark_dirty(int dirfd);
extern int test_supervisor_finish(int dirfd, int marker_fd, int fail_scan);

static int gone(int dirfd) {
    struct stat s;
    return fstatat(dirfd, "dirty", &s, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
}
static int marked(int dirfd) {
    struct stat s;
    return fstatat(dirfd, "dirty", &s, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(s.st_mode);
}
static int scenario(int dirfd, int mode) {
    int report[2], ack[2]; if (pipe(report) != 0 || pipe(ack) != 0) return -1;
    pid_t supervisor = fork();
    if (supervisor < 0) return -1;
    if (supervisor == 0) {
        close(report[0]); close(ack[1]);
        if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) _exit(2);
        int marker = test_supervisor_mark_dirty(dirfd);
        if (marker < 0) _exit(2);
        pid_t worker = fork();
        if (worker < 0) _exit(2);
        if (worker == 0) {
            close(ack[0]); close(report[1]);
            alarm(15);
            if (setsid() < 0) _exit(2);
            for (;;) pause();
        }
        if (write(report[1], &worker, sizeof worker) != sizeof worker) _exit(2);
        close(report[1]);
        char go; if (read(ack[0], &go, 1) != 1) _exit(2);
        close(ack[0]);
        if (mode == 2) for (;;) pause();
        int result = test_supervisor_finish(dirfd, marker, mode == 1);
        close(marker);
        _exit(mode == 0 ? (result == 0 ? 0 : 2) : (result < 0 ? 0 : 2));
    }
    close(report[1]); close(ack[0]); pid_t worker = -1; int status;
    int supervisor_fd = (int)syscall(__NR_pidfd_open, supervisor, 0);
    int ok = read(report[0], &worker, sizeof worker) == sizeof worker && worker > 0 && supervisor_fd >= 0;
    close(report[0]);
    int worker_fd = ok ? (int)syscall(__NR_pidfd_open, worker, 0) : -1;
    if (worker_fd < 0) ok = 0;
    if (ok && write(ack[1], "G", 1) != 1) ok = 0;
    close(ack[1]);
    if (ok && mode == 2 && syscall(__NR_pidfd_send_signal, supervisor_fd, SIGKILL, NULL, 0) != 0) ok = 0;
    if (waitpid(supervisor, &status, 0) != supervisor) ok = 0;
    else if (mode == 2) { if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) ok = 0; }
    else if (!WIFEXITED(status) || WEXITSTATUS(status)) ok = 0;
    if (supervisor_fd >= 0) close(supervisor_fd);
    if (mode == 0) {
        if (!gone(dirfd)) ok = 0;
    } else {
        if (!marked(dirfd)) ok = 0;
        int retry = test_supervisor_mark_dirty(dirfd);
        if (retry >= 0) { close(retry); ok = 0; }
        if (worker_fd >= 0) syscall(__NR_pidfd_send_signal, worker_fd, SIGKILL, NULL, 0);
        while (waitpid(worker, &status, 0) < 0 && errno == EINTR) { }
        if (unlinkat(dirfd, "dirty", 0) != 0) ok = 0;
    }
    if (!ok && mode == 0) {
        if (worker_fd >= 0) syscall(__NR_pidfd_send_signal, worker_fd, SIGKILL, NULL, 0);
        if (worker > 0) while (waitpid(worker, &status, 0) < 0 && errno == EINTR) { }
        if (marked(dirfd)) unlinkat(dirfd, "dirty", 0);
    }
    if (worker_fd >= 0) close(worker_fd);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 2 || prctl(PR_SET_CHILD_SUBREAPER, 1) != 0 || mkdir(argv[1], 0700) != 0) return 2;
    int dirfd = open(argv[1], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dirfd < 0) return 2;
    int normal = scenario(dirfd, 0), failed = scenario(dirfd, 1), crash = scenario(dirfd, 2);
    int clean = gone(dirfd) && close(dirfd) == 0 && rmdir(argv[1]) == 0;
    printf("NORMAL_CLEAN=%d SCAN_FAILURE_DIRTY=%d CRASH_RESTART_REFUSED=%d TEST_DIR_CLEAN=%d\n", normal == 0, failed == 0, crash == 0, clean);
    return normal == 0 && failed == 0 && crash == 0 && clean ? 0 : 1;
}
