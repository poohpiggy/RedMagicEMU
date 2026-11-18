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

int qemu_pipe_open_ns(const char *ns, const char *name, int flags) {
    if (ns || !name || (strcmp(name, "opengles") && strcmp(name, "GLProcessPipe")))
        return fail(EINVAL);
    if (flags != 0 && flags != O_RDWR) return fail(ENOTSUP);
    char selector[sizeof("pipe:GLProcessPipe")];
    int n = snprintf(selector, sizeof(selector), "pipe:%s", name);
    return connect_pipe(selector, (size_t)n + 1);
}

int rme_vulkan_memory_fd(uint64_t memory, int *cached) {
    static const char selector[] = "pipe:RmeVulkanMemory";
    int peer = connect_pipe(selector, sizeof(selector));
    if (peer < 0) return peer;
    int rc = qemu_pipe_write_fully(peer, &memory, sizeof(memory));
    if (rc < 0) {
        close(peer);
        return rc;
    }
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
    close(peer);
    struct cmsghdr *header = CMSG_FIRSTHDR(&response);
    if (count == 1 && (status == 'M' || status == 'C') &&
        !(response.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
        header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
        header->cmsg_len == CMSG_LEN(sizeof(int))) {
        int fd;
        memcpy(&fd, CMSG_DATA(header), sizeof(fd));
        *cached = status == 'C';
        return fd;
    }
    return fail(EINVAL);
}

int qemu_pipe_open(const char *name) { return qemu_pipe_open_ns(NULL, name, O_RDWR); }
void qemu_pipe_close(int fd) { close(fd); }

int qemu_pipe_read(int fd, void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
    ssize_t n = read(fd, buffer, (size_t)size);
    return n < 0 ? -errno : (int)n;
}

int qemu_pipe_write(int fd, const void *buffer, int size) {
    if (size < 0 || (!buffer && size)) return fail(EINVAL);
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
