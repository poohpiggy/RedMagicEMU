#define _GNU_SOURCE
#include "abi.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct socket_stream { int fd; void *mapping; size_t capacity; };

static int read_socket(void *stream, void *data, size_t bytes) {
    return rme_cl_read_exact(((struct socket_stream *)stream)->fd, data, bytes);
}

static int write_socket(void *stream, const struct rme_cl_reply *reply, const void *data, size_t bytes) {
    return rme_cl_write_parts(((struct socket_stream *)stream)->fd, reply, sizeof(*reply), NULL, 0, data, bytes);
}

static void *publish_mapping(int socket, const char *name, size_t bytes) {
    int fd = (int)syscall(SYS_memfd_create, name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return NULL;
    int fixed = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;
    void *mapping = ftruncate(fd, (off_t)bytes) || fcntl(fd, F_ADD_SEALS, fixed) ? MAP_FAILED
        : mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int status = mapping == MAP_FAILED ? -1 : rme_cl_send_mapping(socket, fd, bytes);
    close(fd);
    if (status) {
        if (mapping != MAP_FAILED) munmap(mapping, bytes);
        return NULL;
    }
    return mapping;
}

static void *share_socket(void *opaque, size_t bytes) {
    struct socket_stream *stream = opaque;
    void *mapping = publish_mapping(stream->fd, "rme-cl-buffer", bytes);
    if (!mapping) return NULL;
    if (stream->mapping) munmap(stream->mapping, stream->capacity);
    stream->mapping = mapping;
    stream->capacity = bytes;
    return mapping;
}

static void destroy_readback(struct rme_cl_readback *region) {
    munmap(region->data, RME_CL_READBACK_SIZE);
    free(region);
}

static struct rme_cl_readback *share_readback_socket(void *opaque) {
    struct socket_stream *stream = opaque;
    struct rme_cl_readback *region = malloc(sizeof(*region));
    if (!region) return NULL;
    void *mapping = publish_mapping(stream->fd, "rme-cl-readback", RME_CL_READBACK_SIZE);
    if (!mapping) {
        free(region);
        return NULL;
    }
    *region = (struct rme_cl_readback){.data = mapping, .destroy = destroy_readback, .refs = 1};
    return region;
}

int rme_cl_abi_serve_fd(int fd, void *driver) {
    struct socket_stream stream = {.fd = fd};
    int result = rme_cl_abi_serve(&stream, read_socket, write_socket, share_socket, share_readback_socket, driver, dlsym);
    if (stream.mapping) munmap(stream.mapping, stream.capacity);
    return result;
}

int rme_cl_abi_check_driver(void *driver) { return rme_cl_abi_check(driver, dlsym); }
