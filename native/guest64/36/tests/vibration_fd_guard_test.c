#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned char u8;
struct iov { void *base; u64 len; };
struct msgh { void *name; u32 namelen; struct iov *iov; u64 iovlen; void *control; u64 controllen; int flags; };
struct cmsg { u64 len; int level, type; };
#define VIBRATION_CLOSE(fd) close(fd)
#include "../vibration_fd_guard.h"

static void packet(int sender, int nfd) {
    char payload = 'x';
    struct iovec iov = { &payload, 1 };
    union { struct cmsghdr alignment; char data[CMSG_SPACE(4 * sizeof(int))]; } control;
    memset(&control, 0, sizeof control);
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1 };
    int fds[4];
    for (int i = 0; i < nfd; ++i) {
        fds[i] = nfd == 1 ? memfd_create("aemu-binder-test", MFD_CLOEXEC) : open("/dev/null", O_RDONLY | O_CLOEXEC);
        assert(fds[i] >= 0);
    }
    if (nfd) {
        message.msg_control = control.data;
        message.msg_controllen = CMSG_SPACE((size_t)nfd * sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN((size_t)nfd * sizeof(int));
        memcpy(CMSG_DATA(c), fds, (size_t)nfd * sizeof(int));
    }
    assert(sendmsg(sender, &message, 0) == 1);
    for (int i = 0; i < nfd; ++i) close(fds[i]);
}

static void check(int nfd, int truncate) {
    int pair[2], enabled = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    assert(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled) == 0);
    packet(pair[0], nfd);
    char payload;
    struct iovec iov = { &payload, 1 };
    union { struct cmsghdr alignment; char data[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(4 * sizeof(int))]; } control;
    memset(&control, 0, sizeof control);
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control.data,
        .msg_controllen = truncate ? CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int)) : sizeof(control.data) };
    assert(recvmsg(pair[1], &message, MSG_CMSG_CLOEXEC) == 1 && payload == 'x');
    int received[4], count = 0, credentials = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS) ++credentials;
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int n = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            assert(n >= 0 && n <= 4);
            memcpy(received, CMSG_DATA(c), (size_t)n * sizeof(int)); count = n;
        }
    }
    assert(credentials == 1);
    if (truncate) assert(message.msg_flags & MSG_CTRUNC);
    struct msgh guarded = { .control = message.msg_control, .controllen = message.msg_controllen, .flags = message.msg_flags };
    int result = rme_vibration_check_received(&guarded);
    if (!nfd) assert(result == 0 && guarded.controllen == message.msg_controllen);
    else {
        assert(result == -EPERM && guarded.controllen == 0);
        for (int i = 0; i < count; ++i) { errno = 0; assert(fcntl(received[i], F_GETFD) == -1 && errno == EBADF); }
    }
    close(pair[0]); close(pair[1]);
}

int main(void) {
    check(0, 0);              /* SCM_CREDENTIALS handshake still works. */
    check(1, 0);              /* Includes Binder F_BIG's attached memfd. */
    check(4, 1);              /* Reject truncation; close delivered FDs. */
    union { struct cmsg header; u8 data[32]; } control;
    memset(&control, 0, sizeof control);
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC); assert(fd >= 0);
    control.header.len = sizeof(struct cmsg) + sizeof(int);
    control.header.level = SOL_SOCKET; control.header.type = 4; /* SCM_PIDFD */
    memcpy(control.data + sizeof(struct cmsg), &fd, sizeof fd);
    struct msgh message = { .control = control.data, .controllen = sizeof control.data };
    assert(rme_vibration_check_received(&message) == -EPERM);
    errno = 0; assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    memset(&control, 0, sizeof control);
    control.header.len = sizeof(struct cmsg) - 1;
    message = (struct msgh){ .control = control.data, .controllen = sizeof control.data };
    assert(rme_vibration_check_received(&message) == -EPERM);
    puts("VIBRATION_FD_GUARD_OK");
}
