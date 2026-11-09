#define _GNU_SOURCE
#include "qti_session.h"
#include "qti_led_shim.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Use packet credentials rather than SO_PEERCRED: the Guest runtime can
 * rewrite the latter. Receive once with a bounded wait; reject every extra
 * control message, closing any received descriptors before returning. */
static int receive_hello(int fd, pid_t pid, uid_t uid, uint64_t epoch) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    /* HostVibrationChannel permits a 20-second attach; give it five seconds
     * of scheduling slack while keeping Guest startup bounded. */
    int ready = poll(&p, 1, 25000);
    if (ready == 0) return -ETIMEDOUT;
    if (ready < 0) return -errno;
    unsigned char bytes[12];
    struct iovec io = { .iov_base = bytes, .iov_len = sizeof(bytes) };
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(32 * sizeof(int))]; } control;
    memset(&control, 0, sizeof(control));
    struct msghdr msg = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes) };
    ssize_t got = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (got < 0) return -errno;
    int valid = got == (ssize_t)sizeof(bytes) && !(msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    unsigned count = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_len < CMSG_LEN(0)) { valid = 0; break; }
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS && c->cmsg_len == CMSG_LEN(sizeof(struct ucred))) {
            struct ucred cred; memcpy(&cred, CMSG_DATA(c), sizeof(cred)); ++count;
            if (cred.pid != pid || cred.uid != uid) valid = 0;
        } else {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                size_t length = c->cmsg_len - CMSG_LEN(0);
                size_t nr = length / sizeof(int);
                for (size_t i = 0; i < nr; ++i) { int received; memcpy(&received, CMSG_DATA(c) + i * sizeof(int), sizeof(received)); close(received); }
            }
            valid = 0;
        }
    }
    if (count != 1 || got != (ssize_t)sizeof(bytes) || memcmp(bytes, "RMEH", 4)) return -EACCES;
    uint64_t value = 0;
    for (unsigned i = 4; i < 12; ++i) value = (value << 8) | bytes[i];
    return valid && value == epoch ? 0 : -EACCES;
}

int rme_qti_session_start(struct rme_qti_session *s, const char *name, pid_t pid, uid_t uid, uint64_t epoch) {
    if (!s || !name || pid <= 0 || !epoch || epoch > INT64_MAX) return -EINVAL;
    if (s->active) return -EALREADY;
    size_t n = strnlen(name, 81);
    if (!n || n > 80) return -EINVAL;
    for (size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return -EINVAL;
    }
    s->active = 0;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -errno;
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) < 0) {
        int error = -errno; close(fd); return error;
    }
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path + 1, name, n);
    int result = 0;
    if (connect(fd, (struct sockaddr *)&a, offsetof(struct sockaddr_un, sun_path) + 1 + n) < 0) {
        result = -errno; goto close_fd;
    }
    result = receive_hello(fd, pid, uid, epoch);
    if (result) goto close_fd;
    result = rme_guest_vibration_init(&s->sender, fd, epoch);
    if (result) goto close_fd;
    result = rme_qti_led_init(&s->adapter, &s->sender);
    if (result) goto close_sender;
    result = rme_qti_shim_bind(&s->adapter);
    if (result) { rme_qti_led_destroy(&s->adapter); goto close_sender; }
    s->active = 1;
    return 0;
close_sender:
    rme_guest_vibration_close(&s->sender);
    rme_guest_vibration_destroy(&s->sender);
    return result;
close_fd:
    close(fd);
    return result;
}

int rme_qti_session_close(struct rme_qti_session *s) {
    if (!s) return -EINVAL;
    if (!s->active) return -ESHUTDOWN;
    int result = rme_qti_shim_unbind(&s->adapter);
    if (result) return result; /* The adapter may still be in use. */
    result = rme_guest_vibration_off(&s->sender);
    int error = rme_qti_led_destroy(&s->adapter); if (!result) result = error;
    error = rme_guest_vibration_close(&s->sender); if (!result) result = error;
    error = rme_guest_vibration_destroy(&s->sender); if (!result) result = error;
    s->active = 0;
    return result;
}
