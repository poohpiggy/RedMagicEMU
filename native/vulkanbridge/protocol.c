#define _POSIX_C_SOURCE 200809L
#include "protocol.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

/* Fixed-size, little-endian frames. No received length controls allocation. */
enum { VKB_MAGIC = 0x314b5652u, VKB_HELLO = 1, VKB_REPLY = 2 };

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void header(unsigned char *p, uint32_t type, uint32_t length) {
    put32(p, VKB_MAGIC);
    put32(p + 4, type);
    put32(p + 8, length);
}

static int now_ms(int64_t *out) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    *out = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return 0;
}

static int ready(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t now;
        if (now_ms(&now) < 0) return -1;
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        struct pollfd p = { .fd = fd, .events = events };
        int n = poll(&p, 1, (int)(deadline - now));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) { errno = ETIMEDOUT; return -1; }
        /* Readiness may be delivered only after this thread was descheduled. */
        if (now_ms(&now) < 0) return -1;
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        if (p.revents & POLLNVAL) { errno = EBADF; return -1; }
        /* Read remaining bytes after HUP, or obtain the actual socket error. */
        if (p.revents & (events | POLLHUP | POLLERR)) return 0;
    }
}

static int transfer(int fd, void *data, size_t size, int sending, int64_t deadline) {
    unsigned char *p = data;
    while (size) {
        if (ready(fd, sending ? POLLOUT : POLLIN, deadline) < 0) return -1;
        ssize_t n = sending ? send(fd, p, size, MSG_DONTWAIT | MSG_NOSIGNAL)
                            : recv(fd, p, size, MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) return -1;
        if (n == 0) { errno = ECONNRESET; return -1; }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 0;
}

static int start(int fd, uint32_t version, int timeout_ms, int64_t *deadline) {
    if (!version || timeout_ms < 1 || timeout_ms > 60000) { errno = EINVAL; return -2; }
    if (fd < 0) { errno = EBADF; return -1; }
    if (now_ms(deadline) < 0) return -1;
    *deadline += timeout_ms;
    return 0;
}

static int read_header(int fd, uint32_t type, uint32_t length, int64_t deadline) {
    unsigned char frame[12];
    if (transfer(fd, frame, sizeof(frame), 0, deadline) < 0) return -1;
    if (get32(frame) != VKB_MAGIC || get32(frame + 4) != type || get32(frame + 8) != length) {
        errno = EPROTO;
        return -2;
    }
    return 0;
}

int vkb_client_hello(int fd, uint32_t version, int timeout_ms) {
    int64_t deadline;
    int rc = start(fd, version, timeout_ms, &deadline);
    if (rc) return rc;
    unsigned char request[16], response[8];
    header(request, VKB_HELLO, 4);
    put32(request + 12, version);
    if (transfer(fd, request, sizeof(request), 1, deadline) < 0) return -1;
    rc = read_header(fd, VKB_REPLY, sizeof(response), deadline);
    if (rc) return rc;
    if (transfer(fd, response, sizeof(response), 0, deadline) < 0) return -1;
    uint32_t server_version = get32(response), status = get32(response + 4);
    if (!server_version || status > 1 || (!status && server_version != version)) {
        errno = EPROTO;
        return -2;
    }
    return status ? 1 : 0;
}

int vkb_server_hello(int fd, uint32_t version, int timeout_ms) {
    int64_t deadline;
    int rc = start(fd, version, timeout_ms, &deadline);
    if (rc) return rc;
    rc = read_header(fd, VKB_HELLO, 4, deadline);
    if (rc) return rc;
    unsigned char request[4], response[20];
    if (transfer(fd, request, sizeof(request), 0, deadline) < 0) return -1;
    uint32_t client_version = get32(request);
    if (!client_version) { errno = EPROTO; return -2; }
    uint32_t rejected = client_version != version;
    header(response, VKB_REPLY, 8);
    put32(response + 12, version);
    put32(response + 16, rejected);
    if (transfer(fd, response, sizeof(response), 1, deadline) < 0) return -1;
    return rejected ? 1 : 0;
}
