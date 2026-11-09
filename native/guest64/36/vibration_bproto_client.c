#define _GNU_SOURCE
#include "vibration_bproto_client.h"
#include "bproto.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define BC_TRANSACTION 0x40406300u
#define BC_REPLY 0x40406301u
#ifndef SCM_PIDFD
#define SCM_PIDFD 0x04
#endif

static int plain_binder_object(const void *data, uint64_t offset) {
    uint32_t type;
    memcpy(&type, (const char *)data + offset, sizeof type);
    return type == 0x73622a85u || type == 0x77622a85u ||
           type == 0x73682a85u || type == 0x77682a85u;
}

static int send_packet(int fd, const struct iovec *vec, size_t count, size_t expected) {
    struct msghdr msg = { .msg_iov = (struct iovec *)vec, .msg_iovlen = count };
    ssize_t n;
    do { n = sendmsg(fd, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    if ((size_t)n != expected) { errno = EIO; return -1; }
    return 0;
}

void rme_bproto_close(struct rme_bproto_client *c) {
    if (c && c->fd >= 0) { int fd = c->fd; c->fd = -1; close(fd); }
}

int rme_bproto_open(struct rme_bproto_client *c, int fd, pid_t peer_pid, uid_t peer_uid,
                    unsigned guest_uid, unsigned process_key) {
    if (!c) { if (fd >= 0) close(fd); errno = EINVAL; return -1; }
    c->fd = fd; c->guest_uid = guest_uid;
    if (fd < 0 || peer_pid <= 0 || !process_key) { errno = EINVAL; goto fail; }
    int type = 0; socklen_t tlen = sizeof type;
    struct ucred peer; socklen_t plen = sizeof peer;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tlen) || type != SOCK_SEQPACKET ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &plen) || plen != sizeof peer ||
        peer.pid != peer_pid || peer.uid != peer_uid) { errno = EPERM; goto fail; }
    int fl = fcntl(fd, F_GETFD);
    if (fl < 0 || fcntl(fd, F_SETFD, fl | FD_CLOEXEC) < 0) goto fail;
    struct c_hello hello = { .h = { .op = OP_HELLO, .euid = guest_uid },
        .pid = (unsigned)getpid(), .tid = (unsigned)syscall(SYS_gettid), .main = 1, .pad = process_key };
    struct iovec vec = { &hello, sizeof hello };
    if (send_packet(fd, &vec, 1, sizeof hello)) goto fail;
    return 0;
fail:
    { int error = errno; rme_bproto_close(c); errno = error; return -1; }
}

static int send_txn(struct rme_bproto_client *c, unsigned cmd, unsigned handle, unsigned code,
                    unsigned flags, const void *data, size_t length, const uint64_t *offsets, size_t count) {
    if (!c || c->fd < 0 || (length && !data) || (count && !offsets) || length > INLINE_MAX ||
        count > INLINE_MAX / sizeof(uint64_t)) { errno = EINVAL; return -1; }
    size_t aligned = (length + 7) & ~(size_t)7;
    if ((count ? aligned : length) + count * 8 > INLINE_MAX) { errno = EMSGSIZE; return -1; }
    for (size_t i = 0; i < count; ++i) {
        if (offsets[i] > length || length - (size_t)offsets[i] < 24 ||
            (offsets[i] & 3) || (i && offsets[i] < offsets[i - 1] + 24) ||
            !plain_binder_object(data, offsets[i])) { errno = EINVAL; return -1; }
    }
    struct c_txn m = { .h = { .op = OP_TXN, .euid = c->guest_uid }, .cmd = cmd,
        .handle = handle, .code = code, .tflags = flags, .data_size = length, .offsets_size = count * 8 };
    const unsigned char zero[8] = {0};
    size_t padding = count ? aligned - length : 0;
    struct iovec v[] = {{&m, sizeof m}, {(void *)data, length}, {(void *)zero, padding}, {(void *)offsets, count * 8}};
    return send_packet(c->fd, v, 4, sizeof m + length + padding + count * 8);
}

int rme_bproto_txn(struct rme_bproto_client *c, unsigned handle, unsigned code, unsigned flags,
                   const void *data, size_t length, const uint64_t *offsets, size_t count) {
    return send_txn(c, BC_TRANSACTION, handle, code, flags, data, length, offsets, count);
}
int rme_bproto_reply(struct rme_bproto_client *c, const void *data, size_t length) {
    return send_txn(c, BC_REPLY, 0, 0, 0, data, length, NULL, 0);
}
int rme_bproto_read_request(struct rme_bproto_client *c, unsigned read_size) {
    if (!c || c->fd < 0 || read_size < 128 || read_size > INLINE_MAX) { errno = EINVAL; return -1; }
    struct c_hdr h = { .op = OP_READ, .flags = F_READ, .read_size = read_size, .euid = c->guest_uid };
    struct iovec v = { &h, sizeof h };
    return send_packet(c->fd, &v, 1, sizeof h);
}

int rme_bproto_receive(struct rme_bproto_client *c, struct d_hdr *out, void *payload, size_t cap, size_t *length) {
    if (!c || c->fd < 0 || !out || !length || (cap && !payload) || cap > INLINE_MAX + 4096) { errno = EINVAL; return -1; }
    *length = 0;
    char controls[CMSG_SPACE(sizeof(int) * 32)] = {0};
    struct iovec v[] = {{out, sizeof *out}, {payload, cap}};
    struct msghdr msg = { .msg_iov = v, .msg_iovlen = 2, .msg_control = controls, .msg_controllen = sizeof controls };
    ssize_t n;
    do { n = recvmsg(c->fd, &msg, MSG_CMSG_CLOEXEC); } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    int bad_control = msg.msg_controllen != 0;
    for (struct cmsghdr *h = CMSG_FIRSTHDR(&msg); h; h = CMSG_NXTHDR(&msg, h)) {
        if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS && h->cmsg_len >= CMSG_LEN(0)) {
            size_t count = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; ++i) { int fd; memcpy(&fd, (char *)CMSG_DATA(h) + i * sizeof fd, sizeof fd); close(fd); }
        } else if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_PIDFD && h->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int fd; memcpy(&fd, CMSG_DATA(h), sizeof fd); close(fd);
        }
    }
    if (bad_control || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || n < (ssize_t)sizeof *out ||
        out->br_size % 4 || out->nfds || (out->flags & ~(F_TXN | F_AGAIN | F_WAKE)) || out->extra_size ||
        out->offsets_size % 8 || out->data_size > INLINE_MAX || out->offsets_size > INLINE_MAX) {
        goto protocol_error;
    }
    size_t expected = out->br_size;
    if (out->flags & F_TXN) {
        size_t aligned = ((size_t)out->data_size + 7) & ~(size_t)7;
        if (expected > cap || aligned > cap - expected || out->offsets_size > cap - expected - aligned) goto protocol_error;
        expected += aligned + (size_t)out->offsets_size;
    } else if (out->data_size || out->offsets_size) goto protocol_error;
    if ((size_t)n - sizeof *out != expected) goto protocol_error;
    if (out->flags & F_TXN) {
        size_t aligned = ((size_t)out->data_size + 7) & ~(size_t)7;
        const char *base = payload;
        uint64_t previous = 0;
        for (size_t i = 0; i < out->offsets_size / 8; ++i) {
            uint64_t offset;
            memcpy(&offset, base + out->br_size + aligned + i * 8, sizeof offset);
            if (offset > out->data_size || out->data_size - offset < 24 || (offset & 3) ||
                (i && offset < previous + 24) || !plain_binder_object(base + out->br_size, offset)) goto protocol_error;
            previous = offset;
        }
    }
    *length = expected;
    return 0;
protocol_error:
    rme_bproto_close(c);
    errno = EPROTO;
    return -1;
}
