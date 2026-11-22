/* EMU transport for the gfxstream QEMU-pipe client ABI.
 * The two peers speak the upstream pipe selector followed by the unmodified
 * gfxstream byte stream. This file does not implement a renderer. */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef RME_VULKAN_REPLY_PIPE_TEST
#include <poll.h>
#include <stdatomic.h>
#include <sys/stat.h>
#endif

#include "shared_memory_wire.h"

#ifndef RME_VK_SOCKET_PATH
#define RME_VK_SOCKET_PATH "/dev/socket/aemuvk"
#endif

int qemu_pipe_read_fully(int fd, void *buffer, int size);
int qemu_pipe_write_fully(int fd, const void *buffer, int size);

static int fail(int error) { errno = error; return -error; }

static int connect_pipe(const char *selector, size_t selector_size) {
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    const char path[] = RME_VK_SOCKET_PATH;
    if (sizeof(path) > sizeof(addr.sun_path)) return fail(ENAMETOOLONG);
    memcpy(addr.sun_path, path, sizeof(path));
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;
    int rc;
    do { rc = connect(fd, (const struct sockaddr *)&addr,
                      (socklen_t)(offsetof(struct sockaddr_un, sun_path) + sizeof(path))); }
    while (rc < 0 && errno == EINTR);
    if (rc < 0) { int e = errno; close(fd); return fail(e); }
    rc = qemu_pipe_write_fully(fd, selector, (int)selector_size);
    if (rc < 0) { int e = -rc; close(fd); return fail(e); }
    return fd;
}

#ifdef RME_VULKAN_REPLY_PIPE_TEST
// The Unix socket remains the lifetime handle. Zero denotes an absent
// endpoint. Descriptor fields contain the actual fd plus one.
enum { REPLY_PIPE_LIMIT = 4096 };
static struct pipe_endpoints {
    _Atomic int response;
    _Atomic int command;
    _Atomic int keep_reader;
    _Atomic int broken;
} reply_pipes[REPLY_PIPE_LIMIT];

static int pipe_mode(int fd, int mode, struct stat *info) {
    const int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && (flags & O_ACCMODE) == mode &&
           (mode != O_WRONLY || (flags & O_NONBLOCK)) &&
           fstat(fd, info) == 0 && S_ISFIFO(info->st_mode);
}

static int open_reply_pipe(int duplex) {
    static const char reply[] = "pipe:RmeVulkanReplyPipe";
    static const char both[] = "pipe:RmeVulkanDuplexPipe";
    int peer = connect_pipe(duplex ? both : reply, duplex ? sizeof(both) : sizeof(reply));
    if (peer < 0) return peer;
    if (peer >= REPLY_PIPE_LIMIT) {
        close(peer);
        return fail(EPROTONOSUPPORT);
    }
    char status = 0;
    struct iovec data = { .iov_base = &status, .iov_len = 1 };
    union {
        struct cmsghdr header;
        char bytes[CMSG_SPACE(3 * sizeof(int))];
    } control = {0};
    struct msghdr message = {
        .msg_iov = &data, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes),
    };
    ssize_t count;
    do { count = recvmsg(peer, &message, MSG_CMSG_CLOEXEC); } while (count < 0 && errno == EINTR);
    int error = count == 0 || (count < 0 && errno == ECONNRESET) ? EPROTONOSUPPORT : EPROTO;
    struct cmsghdr *header = count > 0 ? CMSG_FIRSTHDR(&message) : NULL;
    if (header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
        header->cmsg_len >= CMSG_LEN(sizeof(int))) {
        const size_t n = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int fds[3] = {-1, -1, -1};
        struct stat response_info, command_info, reader_info;
        if (n == (duplex ? 3u : 1u) && status == (duplex ? 'D' : 'P') &&
            !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            memcpy(fds, CMSG_DATA(header), n * sizeof(int));
            if (pipe_mode(fds[0], O_RDONLY, &response_info) &&
                (!duplex || (pipe_mode(fds[1], O_WRONLY, &command_info) &&
                             pipe_mode(fds[2], O_RDONLY, &reader_info) &&
                             command_info.st_dev == reader_info.st_dev && command_info.st_ino == reader_info.st_ino &&
                             (response_info.st_dev != command_info.st_dev || response_info.st_ino != command_info.st_ino)))) {
                struct pipe_endpoints *ends = &reply_pipes[peer];
                atomic_store_explicit(&ends->broken, 0, memory_order_relaxed);
                atomic_store_explicit(&ends->keep_reader, fds[2] + 1, memory_order_relaxed);
                atomic_store_explicit(&ends->command, fds[1] + 1, memory_order_relaxed);
                atomic_store_explicit(&ends->response, fds[0] + 1, memory_order_release);
                return peer;
            }
        }
        for (size_t i = 0; i < n; ++i) {
            int fd;
            memcpy(&fd, CMSG_DATA(header) + i * sizeof(fd), sizeof(fd));
            close(fd);
        }
    }
    close(peer);
    return fail(error);
}

static int write_command_pipe(int control, int command, const void *buffer, int size) {
    while (1) {
        const ssize_t count = write(command, buffer, (size_t)size);
        if (count >= 0) return (int)count;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -errno;
        struct pollfd ready[] = {{command, POLLOUT, 0}, {control, 0, 0}};
        const int rc = poll(ready, 2, -1);
        if (rc > 0 && !ready[1].revents && (ready[0].revents & POLLOUT)) continue;
        return rc < 0 ? -errno : fail(EPIPE);
    }
}
#endif

int qemu_pipe_open_ns(const char *ns, const char *name, int flags) {
    if (ns || !name || (strcmp(name, "opengles") && strcmp(name, "GLProcessPipe")))
        return fail(EINVAL);
    if (flags != 0 && flags != O_RDWR) return fail(ENOTSUP);
#ifdef RME_VULKAN_REPLY_PIPE_TEST
    if (!strcmp(name, "opengles")) {
#ifdef RME_VULKAN_DUPLEX_PIPE_TEST
        int peer = open_reply_pipe(1);
        if (peer != -EPROTONOSUPPORT) return peer;
        peer = open_reply_pipe(0);
#else
        int peer = open_reply_pipe(0);
#endif
        if (peer != -EPROTONOSUPPORT) return peer;
    }
#endif
    char selector[sizeof("pipe:GLProcessPipe")];
    int n = snprintf(selector, sizeof(selector), "pipe:%s", name);
    return connect_pipe(selector, (size_t)n + 1);
}

static int request_memory_fd(uint64_t memory, int *mode, int version2) {
    static const char legacy[] = "pipe:RmeVulkanMemory";
    static const char current[] = "pipe:RmeVulkanMemory2";
    const char *selector = version2 ? current : legacy;
    const size_t selector_size = version2 ? sizeof(current) : sizeof(legacy);
    char request[sizeof(current) + sizeof(memory)];
    memcpy(request, selector, selector_size);
    memcpy(request + selector_size, &memory, sizeof(memory));
    int peer = connect_pipe(request, selector_size + sizeof(memory));
    if (peer < 0) return peer;
    char status = 0;
    struct iovec data = { .iov_base = &status, .iov_len = 1 };
    union {
        struct cmsghdr header;
        char bytes[CMSG_SPACE(sizeof(int))];
    } control;
    struct msghdr response = {
        .msg_iov = &data, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes),
    };
    ssize_t count;
    do { count = recvmsg(peer, &response, MSG_CMSG_CLOEXEC); } while (count < 0 && errno == EINTR);
    int receive_error = errno;
    close(peer);
    if (version2 && (count == 0 || (count < 0 && receive_error == ECONNRESET)))
        return fail(EPROTONOSUPPORT);
    struct cmsghdr *header = CMSG_FIRSTHDR(&response);
    if (count == 1 &&
        !(response.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
        header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
        header->cmsg_len == CMSG_LEN(sizeof(int))) {
        int fd;
        memcpy(&fd, CMSG_DATA(header), sizeof(fd));
        if (status == 'M' || status == 'C' || (version2 && status == 'H')) {
            *mode = status == 'M' ? RME_MEMORY_DMA_BUF :
                    status == 'C' ? RME_MEMORY_CACHED : RME_MEMORY_COHERENT;
            return fd;
        }
        close(fd);
    }
    return fail(EINVAL);
}

int rme_vulkan_memory_fd(uint64_t memory, int *mode) {
    int fd = request_memory_fd(memory, mode, 1);
    return fd == -EPROTONOSUPPORT ? request_memory_fd(memory, mode, 0) : fd;
}

int qemu_pipe_open(const char *name) { return qemu_pipe_open_ns(NULL, name, O_RDWR); }
void qemu_pipe_close(int fd) {
#ifdef RME_VULKAN_REPLY_PIPE_TEST
    if ((unsigned)fd < REPLY_PIPE_LIMIT) {
        struct pipe_endpoints *ends = &reply_pipes[fd];
        int reply = atomic_exchange_explicit(&ends->response, 0, memory_order_acq_rel);
        int command = atomic_exchange_explicit(&ends->command, 0, memory_order_relaxed);
        int reader = atomic_exchange_explicit(&ends->keep_reader, 0, memory_order_relaxed);
        if (reply) close(reply - 1);
        if (command) close(command - 1);
        if (reader) close(reader - 1);
    }
#endif
    close(fd);
}

int qemu_pipe_read(int fd, void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
#ifdef RME_VULKAN_REPLY_PIPE_TEST
    if ((unsigned)fd < REPLY_PIPE_LIMIT) {
        struct pipe_endpoints *ends = &reply_pipes[fd];
        int reply = atomic_load_explicit(&ends->response, memory_order_acquire);
        if (reply) {
            const ssize_t n = read(reply - 1, buffer, (size_t)size);
            if (n == 0 && size) atomic_store_explicit(&ends->broken, 1, memory_order_relaxed);
            return n < 0 ? -errno : (int)n;
        }
    }
#endif
    ssize_t n = read(fd, buffer, (size_t)size);
    return n < 0 ? -errno : (int)n;
}

int qemu_pipe_write(int fd, const void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
#ifdef RME_VULKAN_REPLY_PIPE_TEST
    if ((unsigned)fd < REPLY_PIPE_LIMIT) {
        struct pipe_endpoints *ends = &reply_pipes[fd];
        if (atomic_load_explicit(&ends->response, memory_order_acquire)) {
            if (atomic_load_explicit(&ends->broken, memory_order_relaxed)) return fail(EPIPE);
            int command = atomic_load_explicit(&ends->command, memory_order_relaxed);
            if (command) return write_command_pipe(fd, command - 1, buffer, size);
        }
    }
#endif
    ssize_t n = send(fd, buffer, (size_t)size, MSG_NOSIGNAL);
    return n < 0 ? -errno : (int)n;
}

int qemu_pipe_try_again(int result) { return result == -EINTR; }
void qemu_pipe_print_error(int fd) {
    int error = errno;
    fprintf(stderr, "EMU Vulkan pipe %d: %s\n", fd, strerror(error));
    errno = error;
}

int qemu_pipe_read_fully(int fd, void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
    char *p = buffer;
    while (size) {
        int n = qemu_pipe_read(fd, p, size);
        if (qemu_pipe_try_again(n)) continue;
        if (n < 0) return n;
        if (!n) return fail(EPIPE);
        p += n; size -= n;
    }
    return 0;
}

int qemu_pipe_write_fully(int fd, const void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
    const char *p = buffer;
    while (size) {
        int n = qemu_pipe_write(fd, p, size);
        if (qemu_pipe_try_again(n)) continue;
        if (n < 0) return n;
        if (!n) return fail(EIO);
        p += n; size -= n;
    }
    return 0;
}
