#ifndef RME_OPENCL_ABI_H
#define RME_OPENCL_ABI_H

#include <stddef.h>
#include <stdint.h>

#define RME_CL_ABI_MAGIC UINT32_C(0x324c4352)
#define RME_CL_READ_READY_MAGIC UINT32_C(0x524c4352)
#define RME_CL_ABI_MAX (128u << 20)
#define RME_CL_SHARED_MIN (64u << 10)
#define RME_CL_SHARED_DATA (UINT32_C(1) << 31)
#define RME_CL_ASYNC_RELEASE (UINT32_C(1) << 30)
#define RME_CL_READ_SHARED_READY (UINT32_C(1) << 29)
#define RME_CL_CAP_INLINE_RELEASE UINT32_C(1)
#define RME_CL_CAP_ASYNC_READ UINT32_C(2)
#define RME_CL_CAP_READBACK_SLOTS UINT32_C(4)
#define RME_CL_READBACK_SLOT_SIZE (16u << 20)
#define RME_CL_READBACK_SLOT_COUNT 4u
#define RME_CL_READBACK_SIZE (RME_CL_READBACK_SLOT_SIZE * RME_CL_READBACK_SLOT_COUNT)
#define RME_CL_READBACK_FIRST_SLOT 2u
#define RME_CL_LIST_MAX 256u
#ifndef RME_CL_SOCKET_PATH
#define RME_CL_SOCKET_PATH "/dev/socket/aemucl"
#endif

enum rme_cl_kind { PLATFORM = 1, DEVICE, CONTEXT, QUEUE, MEMORY, PROGRAM, KERNEL, EVENT, SAMPLER };
enum rme_cl_command {
    PLATFORMS = 1, DEVICES, INFO, CREATE_CONTEXT, CREATE_QUEUE, CREATE_BUFFER, SOURCE, BUILD_PROGRAM,
    CREATE_KERNEL, SET_ARG, DISPATCH, READ_BUFFER, WRITE_BUFFER, COPY_BUFFER, FLUSH, FINISH,
    RETAIN, RELEASE, WAIT_EVENTS, CREATE_IMAGE, IMAGE_FORMATS, READ_IMAGE, WRITE_IMAGE,
    CREATE_SAMPLER, CREATE_SUB_BUFFER, BINARY, BINARIES, MARKER, BARRIER, FILL_BUFFER, COPY_IMAGE, IMAGE_LAYOUT,
    IMAGE_TO_BUFFER, BUFFER_TO_IMAGE, FILL_IMAGE, TASK, MIGRATE, KERNELS, BUILTIN_PROGRAM, COMPILE_PROGRAM,
    LINK_PROGRAM, UNLOAD_COMPILER, READ_BUFFER_RECT, WRITE_BUFFER_RECT, COPY_BUFFER_RECT, SHARE_BUFFER,
    READ_BUFFER_ASYNC, SHARE_READBACK,
};

enum { INFO_BUILD = 1, INFO_WORKGROUP, INFO_ARG, INFO_IMAGE };

#define RME_CL_HANDLE_TAG UINT64_C(0x524d450000000000)
static inline uint64_t rme_cl_handle(uint32_t id, unsigned kind) {
    return id ? RME_CL_HANDLE_TAG | ((uint64_t)id << 8) | kind : 0;
}

/* The wire endpoints target little-endian 64-bit processes. Values that refer
 * to driver objects carry a session ID. Driver pointers stay in the host. */
struct rme_cl_request {
    uint32_t magic, command, bytes, wait_count;
    uint64_t args[11];
    uint32_t event_requested, release_event;
};

struct rme_cl_reply {
    uint32_t magic;
    int32_t status;
    uint32_t bytes, object;
    uint64_t value;
};

/* A READ_READY reply precedes the regular reply. value identifies the read,
 * status is its native event status, and object identifies the payload location:
 * 0 is the stream, 1 is the regular mapping, 2 and above are readback slots. */

struct rme_cl_readback {
    unsigned char *data;
    void (*destroy)(struct rme_cl_readback *region);
    unsigned refs, occupied;
};

#ifdef __cplusplus
#define RME_ASSERT static_assert
extern "C" {
#else
#define RME_ASSERT _Static_assert
#endif
RME_ASSERT(sizeof(struct rme_cl_request) == 112, "OpenCL request layout");
RME_ASSERT(sizeof(struct rme_cl_reply) == 24, "OpenCL reply layout");
RME_ASSERT(sizeof(size_t) == sizeof(uint64_t), "OpenCL bridge requires a 64-bit process");
#undef RME_ASSERT

typedef int (*rme_cl_abi_read)(void *stream, void *data, size_t bytes);
typedef int (*rme_cl_abi_write)(void *stream, const struct rme_cl_reply *reply, const void *data, size_t bytes);
typedef void *(*rme_cl_abi_resolve)(void *driver, const char *name);
/* Sends the setup reply and descriptors, then returns the new shared mapping. */
typedef void *(*rme_cl_abi_share)(void *stream, size_t bytes);
typedef struct rme_cl_readback *(*rme_cl_abi_share_readback)(void *stream);
int rme_cl_abi_serve(void *stream, rme_cl_abi_read read, rme_cl_abi_write write, rme_cl_abi_share share,
                     rme_cl_abi_share_readback share_readback, void *driver, rme_cl_abi_resolve resolve);
int rme_cl_abi_check(void *driver, rme_cl_abi_resolve resolve);

int rme_cl_read_exact(int fd, void *data, size_t bytes);
int rme_cl_write_parts(int fd, const void *header, size_t header_size, const void *prefix, size_t prefix_size,
                       const void *data, size_t bytes);
int rme_cl_send_mapping(int fd, int mapping, size_t bytes);
int rme_cl_read_mapping(int fd, size_t bytes);
int rme_cl_abi_serve_fd(int fd, void *driver);
int rme_cl_abi_check_driver(void *driver);

#ifdef __cplusplus
}
#endif
#endif
