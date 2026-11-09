#define _GNU_SOURCE
#include "gate.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum { MAX_FRAME = 23 + 256, MAX_RIGHTS = 16 };

/* The caller must obtain this descriptor through the authenticated launcher. */
static int live_pidfd(int fd, int32_t expected) {
    if (fd < 0) return 0;
    struct pollfd pollfd = { .fd = fd, .events = POLLIN };
    if (poll(&pollfd, 1, 0) != 0) return 0;
    char path[64], data[1024];
    if (snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd) >= (int)sizeof(path)) return 0;
    int info = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (info < 0) return 0;
    ssize_t n = read(info, data, sizeof(data) - 1); close(info);
    if (n <= 0 || n >= (ssize_t)sizeof(data) - 1) return 0;
    data[n] = 0;
    const char *pid = strstr(data, "\nPid:\t");
    if (!pid) return 0;
    char *end = NULL; long value = strtol(pid + 6, &end, 10);
    return end && (*end == '\n' || *end == '\0') && value == expected;
}

static int secure_socket(int fd, const struct rme_gate *g) {
    int type = 0, passcred = 0; socklen_t size = sizeof(int);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) || size != sizeof(int) || type != SOCK_SEQPACKET) return 0;
    size = sizeof(int);
    if (getsockopt(fd, SOL_SOCKET, SO_PASSCRED, &passcred, &size) || size != sizeof(int) || passcred != 1) return 0;
    struct sockaddr_un local = {0}, remote = {0}; size = sizeof(local);
    if (getsockname(fd, (struct sockaddr *)&local, &size) || local.sun_family != AF_UNIX) return 0;
    size = sizeof(remote);
    if (getpeername(fd, (struct sockaddr *)&remote, &size) || remote.sun_family != AF_UNIX) return 0;
    struct ucred peer = {0}; size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || size != sizeof(peer)) return 0;
    return peer.pid == g->pid && peer.uid == g->uid;
}

int rme_gate_receive(struct rme_gate *g, int fd, int pidfd,
                     uint8_t *output, size_t capacity, size_t *output_length) {
    if (!output_length) return -1;
    *output_length = 0;
    if (!g || !output || !secure_socket(fd, g) || !live_pidfd(pidfd, g->pid)) return -1;
    uint8_t frame[MAX_FRAME];
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(MAX_RIGHTS * sizeof(int))]; } control = {0};
    struct iovec iov = { .iov_base = frame, .iov_len = sizeof(frame) };
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
                              .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes) };
    ssize_t n = recvmsg(fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
    struct rme_credential credential = {0}; size_t count = 0; int unexpected = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_len < CMSG_LEN(0)) { unexpected = 1; break; }
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS && c->cmsg_len == CMSG_LEN(sizeof(struct ucred))) {
            struct ucred peer; memcpy(&peer, CMSG_DATA(c), sizeof(peer));
            credential.pid = peer.pid; credential.uid = peer.uid; ++count;
        } else if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            size_t bytes = c->cmsg_len - CMSG_LEN(0);
            for (size_t offset = 0; offset + sizeof(int) <= bytes; offset += sizeof(int)) {
                int received; memcpy(&received, (char *)CMSG_DATA(c) + offset, sizeof(received)); close(received);
            }
            unexpected = 1;
        } else unexpected = 1;
    }
    if (unexpected || n < 0 || (size_t)n > capacity || !live_pidfd(pidfd, g->pid) ||
            !rme_gate_accept(g, frame, (size_t)n, message.msg_flags, &credential, count)) return -1;
    memcpy(output, frame, (size_t)n); *output_length = (size_t)n; return 1;
}
