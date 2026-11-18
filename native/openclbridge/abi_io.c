#define _GNU_SOURCE
#include "abi.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int rme_cl_read_exact(int fd, void *data, size_t bytes) {
    unsigned char *p = data;
    size_t read_bytes = 0;
    while (bytes) {
        ssize_t n = recv(fd, p, bytes, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return n == 0 && !read_bytes ? 0 : -1;
        p += n;
        bytes -= (size_t)n;
        read_bytes += (size_t)n;
    }
    return 1;
}

int rme_cl_write_parts(int fd, const void *header, size_t header_size, const void *prefix, size_t prefix_size,
                       const void *data, size_t bytes) {
    struct iovec parts[3] = {{(void *)header, header_size}};
    size_t count = 1;
    if (prefix_size) parts[count++] = (struct iovec){(void *)prefix, prefix_size};
    if (bytes) parts[count++] = (struct iovec){(void *)data, bytes};
    struct msghdr message = {.msg_iov = parts, .msg_iovlen = count};
    while (message.msg_iovlen) {
        ssize_t n = sendmsg(fd, &message, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        while (message.msg_iovlen && (size_t)n >= message.msg_iov[0].iov_len) {
            n -= (ssize_t)message.msg_iov[0].iov_len;
            ++message.msg_iov;
            --message.msg_iovlen;
        }
        if (message.msg_iovlen && n) {
            message.msg_iov[0].iov_base = (char *)message.msg_iov[0].iov_base + n;
            message.msg_iov[0].iov_len -= (size_t)n;
        }
    }
    return 0;
}

union fd_control {
    struct cmsghdr align;
    char bytes[CMSG_SPACE(sizeof(int))];
};

int rme_cl_send_mapping(int fd, int mapping, size_t bytes) {
    struct rme_cl_reply reply = {.magic = RME_CL_ABI_MAGIC, .value = bytes};
    struct iovec payload = {&reply, sizeof(reply)};
    union fd_control control = {0};
    struct msghdr message = {.msg_iov = &payload, .msg_iovlen = 1, .msg_control = control.bytes,
                             .msg_controllen = sizeof(control.bytes)};
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(mapping));
    memcpy(CMSG_DATA(header), &mapping, sizeof(mapping));
    ssize_t n;
    do { n = sendmsg(fd, &message, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    if (n <= 0) return -1;
    if ((size_t)n == sizeof(reply)) return 0;
    return rme_cl_write_parts(fd, (const char *)&reply + n, sizeof(reply) - (size_t)n, NULL, 0, NULL, 0);
}

/* -1 was a valid unsupported reply. -2 means the stream is unusable. */
int rme_cl_read_mapping(int fd, size_t bytes) {
    struct rme_cl_reply reply;
    struct iovec payload = {&reply, sizeof(reply)};
    union fd_control control = {0};
    struct msghdr message = {.msg_iov = &payload, .msg_iovlen = 1, .msg_control = control.bytes,
                             .msg_controllen = sizeof(control.bytes)};
    ssize_t n;
    do { n = recvmsg(fd, &message, MSG_CMSG_CLOEXEC); } while (n < 0 && errno == EINTR);
    if (n <= 0) return -2;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    int mapping = -1;
    if (header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
        header->cmsg_len == CMSG_LEN(sizeof(mapping))) memcpy(&mapping, CMSG_DATA(header), sizeof(mapping));
    if (((size_t)n < sizeof(reply) &&
         rme_cl_read_exact(fd, (char *)&reply + n, sizeof(reply) - (size_t)n) != 1) ||
        reply.magic != RME_CL_ABI_MAGIC || reply.bytes || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        (!reply.status && (mapping < 0 || reply.value != bytes))) {
        if (mapping >= 0) close(mapping);
        return -2;
    }
    if (reply.status && mapping >= 0) { close(mapping); return -2; }
    return mapping;
}
