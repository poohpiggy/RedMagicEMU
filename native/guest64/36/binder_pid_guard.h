#ifndef RME_BINDER_PID_GUARD_H
#define RME_BINDER_PID_GUARD_H
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* SO_PEERPIDFD returns a handle for the socket's original peer without a
 * PID-to-pidfd race. Old kernels reject it: strict mode must fail closed. */
#ifndef SO_PEERPIDFD
#define SO_PEERPIDFD 77
#endif

struct binder_pid_guard { pid_t pid; uid_t uid; int pidfd; };

static int bpg_live(const struct binder_pid_guard *guard) {
    if (!guard || guard->pidfd < 0 || guard->pid <= 0) return 0;
    struct pollfd item = { .fd = guard->pidfd, .events = POLLIN };
    return poll(&item, 1, 0) == 0;
}

static int bpg_init(struct binder_pid_guard *guard, int fd) {
    if (!guard) { errno = EINVAL; return -1; }
    *guard = (struct binder_pid_guard){ .pidfd = -1 };
    int type = 0, passcred = 0;
    socklen_t len = sizeof type;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) || len != sizeof type || type != SOCK_SEQPACKET) {
        errno = EINVAL; return -1;
    }
    len = sizeof passcred;
    if (getsockopt(fd, SOL_SOCKET, SO_PASSCRED, &passcred, &len) || len != sizeof passcred || passcred != 1) {
        errno = EINVAL; return -1;
    }
    struct ucred peer = {0}; len = sizeof peer;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len) || len != sizeof peer ||
        peer.pid <= 0 || peer.uid != getuid()) { errno = EINVAL; return -1; }
    int pidfd = -1; len = sizeof pidfd;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &pidfd, &len)) return -1;
    if (len != sizeof pidfd || pidfd < 0) { if (pidfd >= 0) close(pidfd); errno = EINVAL; return -1; }
    int flags = fcntl(pidfd, F_GETFD);
    if (flags < 0 || fcntl(pidfd, F_SETFD, flags | FD_CLOEXEC)) { close(pidfd); return -1; }
    *guard = (struct binder_pid_guard){ .pid = peer.pid, .uid = peer.uid, .pidfd = pidfd };
    if (!bpg_live(guard)) { close(pidfd); guard->pidfd = -1; errno = ESRCH; return -1; }
    return 0;
}

/* Rights are consumed by Binder; only the process identity is verified here. */
static int bpg_packet(const struct binder_pid_guard *guard, struct msghdr *message) {
    if (!message || !bpg_live(guard) || (message->msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return 0;
    int count = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(message); c; c = CMSG_NXTHDR(message, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_len < CMSG_LEN(0)) return 0;
        if (c->cmsg_type == SCM_RIGHTS) {
            if ((c->cmsg_len - CMSG_LEN(0)) % sizeof(int)) return 0;
            continue;
        }
        if (c->cmsg_type != SCM_CREDENTIALS || c->cmsg_len != CMSG_LEN(sizeof(struct ucred))) return 0;
        struct ucred peer; memcpy(&peer, CMSG_DATA(c), sizeof peer);
        if (++count != 1 || peer.pid != guard->pid || peer.uid != guard->uid) return 0;
    }
    return count == 1 && bpg_live(guard);
}

static void bpg_close(struct binder_pid_guard *guard) {
    if (guard && guard->pidfd >= 0) { close(guard->pidfd); guard->pidfd = -1; }
}
static int bpg_probe(void) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) return -1;
    int passcred = 1, result = -1, saved;
    struct binder_pid_guard guard = { .pidfd = -1 };
    if (!setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &passcred, sizeof passcred))
        result = bpg_init(&guard, pair[0]);
    saved = errno;
    bpg_close(&guard);
    close(pair[0]); close(pair[1]);
    errno = saved;
    return result;
}
#endif
