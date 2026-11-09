#define _GNU_SOURCE
#include "vibration_binder_auth.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif

/* Pid is supplied by the kernel for pidfds in our own fd table. Never infer
 * identity from a caller-supplied number or from pidfd liveness alone. */
static int pidfd_pid(int fd, pid_t *pid) {
    char path[64], buf[1024];
    if (snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd) <= 0) return -1;
    int info = open(path, O_RDONLY | O_CLOEXEC);
    if (info < 0) return -1;
    ssize_t n; do { n = read(info, buf, sizeof buf - 1); } while (n < 0 && errno == EINTR);
    int err = errno; close(info); errno = err;
    if (n <= 0 || n == (ssize_t)sizeof(buf) - 1) return -1;
    buf[n] = 0;
    int found = 0;
    char *line = buf;
    while (*line) {
        char *end = strchr(line, '\n');
        if (!end) return -1;
        *end = 0;
        if (!strncmp(line, "Pid:\t", 5)) {
            char *tail; errno = 0; long v = strtol(line + 5, &tail, 10);
            if (found || errno || *tail || v <= 0 || v > INT_MAX) return -1;
            *pid = (pid_t)v; found = 1;
        }
        line = end + 1;
    }
    return found ? 0 : -1;
}

int rme_binder_auth_alive(const struct rme_binder_auth *a) {
    if (!a || a->socket_fd < 0 || a->pidfd < 0) return 0;
    pid_t p = -1;
    struct ucred peer; socklen_t n = sizeof peer;
    return pidfd_pid(a->pidfd, &p) == 0 && p == a->peer_pid &&
        syscall(SYS_pidfd_send_signal, a->pidfd, 0, NULL, 0) == 0 &&
        getsockopt(a->socket_fd, SOL_SOCKET, SO_PEERCRED, &peer, &n) == 0 &&
        n == sizeof peer && peer.pid == a->peer_pid && peer.uid == a->peer_uid;
}

void rme_binder_auth_close(struct rme_binder_auth *a) {
    if (!a) return;
    if (a->socket_fd >= 0) close(a->socket_fd);
    if (a->pidfd >= 0) close(a->pidfd);
    a->socket_fd = a->pidfd = -1;
}

int rme_binder_auth_open(struct rme_binder_auth *a, int socket_fd, int pidfd, pid_t pid, uid_t uid) {
    if (!a) { errno = EINVAL; return -1; }
    *a = (struct rme_binder_auth){ .socket_fd = -1, .pidfd = -1, .peer_pid = pid, .peer_uid = uid };
    if (socket_fd < 0 || pidfd < 0 || pid <= 0) { errno = EINVAL; return -1; }
    a->socket_fd = fcntl(socket_fd, F_DUPFD_CLOEXEC, 0);
    a->pidfd = fcntl(pidfd, F_DUPFD_CLOEXEC, 0);
    int type = 0; socklen_t len = sizeof type;
    if (a->socket_fd < 0 || a->pidfd < 0 ||
        getsockopt(a->socket_fd, SOL_SOCKET, SO_TYPE, &type, &len) ||
        len != sizeof type || type != SOCK_SEQPACKET || !rme_binder_auth_alive(a)) {
        rme_binder_auth_close(a); errno = EPERM; return -1;
    }
    return 0;
}
