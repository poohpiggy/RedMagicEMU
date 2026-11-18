#define _POSIX_C_SOURCE 200809L
#include "abi.h"

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include "abi_image.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t wire = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t maps_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t fork_setup = PTHREAD_ONCE_INIT;
static int connection = -1;
static unsigned char *shared_buffer;
static size_t shared_capacity;
static int shared_unavailable;
static unsigned char *readback_mapping;
static int readback_unavailable;
static uint32_t peer_caps, pending_release;

struct pending_read {
    struct pending_read *next;
    uint64_t ticket;
    void *data;
    size_t size;
};
static struct pending_read *reads;
static uint64_t read_ticket;

struct event_ref { struct event_ref *next; uint32_t id, refs; };
enum { EVENT_BUCKETS = 128 };
static struct event_ref *events[EVENT_BUCKETS];

struct mapping {
    struct mapping *next;
    cl_mem memory;
    cl_map_flags flags;
    int image;
    size_t offset, size;
    size_t origin[3], region[3];
    void *data;
    int borrowed;
};
static struct mapping *maps;

struct host_buffer {
    struct host_buffer *next;
    cl_mem memory;
    void *pointer;
    cl_mem_flags flags;
    size_t size;
    unsigned refs;
};
static struct host_buffer *host_buffers;

static void before_fork(void) { pthread_mutex_lock(&maps_lock); pthread_mutex_lock(&wire); }
static void after_parent(void) { pthread_mutex_unlock(&wire); pthread_mutex_unlock(&maps_lock); }
static void unmap_shared(void) {
    if (shared_buffer) munmap(shared_buffer, shared_capacity);
    shared_buffer = NULL;
    shared_capacity = 0;
}
static void unmap_readback(void) {
    if (readback_mapping) munmap(readback_mapping, RME_CL_READBACK_SIZE);
    readback_mapping = NULL;
}
static void after_child(void) {
    if (connection >= 0) close(connection);
    connection = -1;
    unmap_shared();
    unmap_readback();
    shared_unavailable = 0;
    readback_unavailable = 0;
    peer_caps = pending_release = 0;
    reads = NULL;
    read_ticket = 0;
    memset(events, 0, sizeof(events));
    maps = NULL;
    host_buffers = NULL;
    after_parent();
}
static void install_fork_hooks(void) { pthread_atfork(before_fork, after_parent, after_child); }

static int connect_bridge(void) {
    struct sockaddr_un address = {.sun_family = AF_UNIX, .sun_path = RME_CL_SOCKET_PATH};
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int status;
    do { status = connect(fd, (const struct sockaddr *)&address, sizeof(address)); } while (status < 0 && errno == EINTR);
    if (status) { close(fd); return -1; }
    return fd;
}

static uint32_t id(const void *object, unsigned kind) {
    uintptr_t value = (uintptr_t)object;
    uint32_t number = (uint32_t)(value >> 8);
    return value == rme_cl_handle(number, kind) ? number : 0;
}

static void *handle(uint32_t object, unsigned kind) { return (void *)(uintptr_t)rme_cl_handle(object, kind); }

static struct event_ref **find_event(uint32_t token) {
    struct event_ref **link = &events[token % EVENT_BUCKETS];
    while (*link && (*link)->id != token) link = &(*link)->next;
    return link;
}

static void disconnect(void) {
    close(connection);
    connection = -2;
    peer_caps = pending_release = 0;
    unmap_shared();
    unmap_readback();
    while (reads) {
        struct pending_read *read = reads;
        reads = read->next;
        free(read);
    }
    for (size_t i = 0; i < EVENT_BUCKETS; ++i) {
        while (events[i]) {
            struct event_ref *entry = events[i];
            events[i] = entry->next;
            free(entry);
        }
    }
}

static struct pending_read **find_read(uint64_t ticket) {
    struct pending_read **link = &reads;
    while (*link && (*link)->ticket != ticket) link = &(*link)->next;
    return link;
}

static int read_reply(struct rme_cl_reply *reply) {
    while (rme_cl_read_exact(connection, reply, sizeof(*reply)) == 1) {
        if (reply->magic == RME_CL_ABI_MAGIC) return 1;
        if (reply->magic != RME_CL_READ_READY_MAGIC ||
            reply->object >= RME_CL_READBACK_FIRST_SLOT + RME_CL_READBACK_SLOT_COUNT || reply->status > CL_COMPLETE)
            return 0;
        struct pending_read **link = find_read(reply->value);
        struct pending_read *read = *link;
        if (!read) return 0;
        if (reply->status == CL_COMPLETE) {
            if (reply->bytes != read->size) return 0;
            if (reply->object >= RME_CL_READBACK_FIRST_SLOT) {
                if (!readback_mapping || read->size > RME_CL_READBACK_SLOT_SIZE) return 0;
                size_t slot = reply->object - RME_CL_READBACK_FIRST_SLOT;
                memcpy(read->data, readback_mapping + slot * RME_CL_READBACK_SLOT_SIZE, reply->bytes);
            } else if (reply->object == 1) {
                if (reply->bytes > shared_capacity) return 0;
                memcpy(read->data, shared_buffer, reply->bytes);
            } else if (rme_cl_read_exact(connection, read->data, reply->bytes) != 1) return 0;
        } else if (reply->bytes || reply->object) return 0;
        *link = read->next;
        free(read);
    }
    return 0;
}

/* Called with the stream locked. Keep a single reusable area per connection. */
static int use_shared(size_t size) {
    if (size < RME_CL_SHARED_MIN) return 0;
    if (size <= shared_capacity) return 1;
    if (shared_unavailable) return 0;
    size_t capacity = RME_CL_SHARED_MIN;
    while (capacity < size) capacity *= 2;
    struct rme_cl_request request = {.magic = RME_CL_ABI_MAGIC, .command = SHARE_BUFFER, .args = {capacity}};
    if (rme_cl_write_parts(connection, &request, sizeof(request), NULL, 0, NULL, 0)) return -1;
    int fd = rme_cl_read_mapping(connection, capacity);
    if (fd == -2) return -1;
    if (fd < 0) { shared_unavailable = 1; return 0; }
    void *mapping = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    unmap_shared();
    if (mapping == MAP_FAILED) { shared_unavailable = 1; return 0; }
    shared_buffer = mapping;
    shared_capacity = capacity;
    return 1;
}

static int use_readback(size_t size) {
    if (size < RME_CL_SHARED_MIN || size > RME_CL_READBACK_SLOT_SIZE ||
        !(peer_caps & RME_CL_CAP_READBACK_SLOTS) || readback_unavailable)
        return 0;
    if (readback_mapping) return 1;
    struct rme_cl_request request = {.magic = RME_CL_ABI_MAGIC, .command = SHARE_READBACK};
    if (rme_cl_write_parts(connection, &request, sizeof(request), NULL, 0, NULL, 0)) return -1;
    int fd = rme_cl_read_mapping(connection, RME_CL_READBACK_SIZE);
    if (fd == -2) return -1;
    if (fd >= 0) {
        void *mapping = mmap(NULL, RME_CL_READBACK_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (mapping != MAP_FAILED) {
            readback_mapping = mapping;
            return 1;
        }
    }
    readback_unavailable = 1;
    return 0;
}

static struct rme_cl_reply exchange(struct rme_cl_request request, const void *data, size_t bytes,
                                    cl_uint count, const cl_event *wait, void *output, size_t capacity) {
    const uint32_t operation = request.command;
    struct rme_cl_reply reply = {.status = CL_INVALID_EVENT_WAIT_LIST};
    uint32_t dependencies[RME_CL_LIST_MAX];
    if (count > RME_CL_LIST_MAX || (count != 0) != (wait != NULL)) return reply;
    for (cl_uint i = 0; i < count; ++i) {
        dependencies[i] = id(wait[i], EVENT);
        if (!dependencies[i]) return reply;
    }
    size_t wait_bytes = count * sizeof(*dependencies);
    if (bytes > RME_CL_ABI_MAX - wait_bytes || capacity > RME_CL_ABI_MAX)
        return (struct rme_cl_reply){.status = CL_OUT_OF_RESOURCES};
    request.magic = RME_CL_ABI_MAGIC;
    request.bytes = (uint32_t)(wait_bytes + bytes);
    request.wait_count = count;
    pthread_once(&fork_setup, install_fork_hooks);
    pthread_mutex_lock(&wire);
    if (connection == -1) connection = connect_bridge();
    if (connection >= 0) {
        uint64_t ticket = 0;
        int slots = 0;
        if (request.command == READ_BUFFER && request.args[10] && capacity && (peer_caps & RME_CL_CAP_ASYNC_READ)) {
            struct pending_read *read = malloc(sizeof(*read));
            if (!read) {
                pthread_mutex_unlock(&wire);
                return (struct rme_cl_reply){.status = CL_OUT_OF_HOST_MEMORY};
            }
            ticket = ++read_ticket;
            *read = (struct pending_read){reads, ticket, output, capacity};
            reads = read;
            request.command = READ_BUFFER_ASYNC;
            request.args[4] = ticket;
            slots = use_readback(capacity);
            request.args[5] = slots > 0;
        }
        size_t data_size = slots > 0 || request.bytes > capacity ? request.bytes : capacity;
        int shared = slots < 0 ? -1 : use_shared(data_size);
        if (shared > 0) {
            request.command |= RME_CL_SHARED_DATA;
            if (wait_bytes) memcpy(shared_buffer, dependencies, wait_bytes);
            if (bytes) memcpy(shared_buffer + wait_bytes, data, bytes);
        } else if (shared_buffer && (peer_caps & RME_CL_CAP_ASYNC_READ)) {
            request.command |= RME_CL_READ_SHARED_READY;
        }
        request.release_event = pending_release;
        pending_release = 0;
        if (shared >= 0 &&
            !rme_cl_write_parts(connection, &request, sizeof(request), dependencies, shared ? 0 : wait_bytes,
                                data, shared ? 0 : bytes) &&
            read_reply(&reply) && reply.bytes <= capacity) {
            if (shared && reply.bytes) memcpy(output, shared_buffer, reply.bytes);
            if (shared || !reply.bytes || rme_cl_read_exact(connection, output, reply.bytes) == 1) {
                if (operation == PLATFORMS && !reply.status) peer_caps = reply.object;
                if (ticket && reply.status) {
                    struct pending_read **link = find_read(ticket);
                    if (*link) {
                        struct pending_read *read = *link;
                        *link = read->next;
                        free(read);
                    }
                }
                pthread_mutex_unlock(&wire);
                return reply;
            }
        }
        disconnect();
    }
    pthread_mutex_unlock(&wire);
    return (struct rme_cl_reply){.status = operation == PLATFORMS ? CL_PLATFORM_NOT_FOUND_KHR : CL_DEVICE_NOT_AVAILABLE};
}

#define REQUEST(op, ...) ((struct rme_cl_request){.command = (op), .args = {__VA_ARGS__}})
#define CALL(request) exchange(request, NULL, 0, 0, NULL, NULL, 0)
#define PAYLOAD(request, data, size) exchange(request, data, size, 0, NULL, NULL, 0)

static void *create_result(struct rme_cl_reply reply, unsigned kind, cl_int *error) {
    if (error) *error = reply.status;
    return reply.status ? NULL : handle(reply.object, kind);
}

static void *create_error(unsigned kind, cl_int *error, cl_int status) {
    return create_result((struct rme_cl_reply){.status = status}, kind, error);
}

static cl_int enumerate(struct rme_cl_request request, unsigned kind, cl_uint entries,
                        void *items, cl_uint *count) {
    if ((!items && !count) || (items && !entries)) return CL_INVALID_VALUE;
    uint32_t ids[RME_CL_LIST_MAX];
    struct rme_cl_reply reply = exchange(request, NULL, 0, 0, NULL, ids, sizeof(ids));
    if (reply.status) return reply.status;
    if (count) *count = (cl_uint)reply.value;
    for (size_t i = 0; items && i < reply.bytes / sizeof(*ids) && i < entries; ++i) {
        void *item = handle(ids[i], kind);
        memcpy((char *)items + i * sizeof(item), &item, sizeof(item));
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clGetPlatformIDs(cl_uint n, cl_platform_id *platforms, cl_uint *count) {
    return enumerate(REQUEST(PLATFORMS, platforms ? n : 0), PLATFORM, n, platforms, count);
}

CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id platform, cl_device_type type, cl_uint n,
                                              cl_device_id *devices, cl_uint *count) {
    return enumerate(REQUEST(DEVICES, id(platform, PLATFORM), type, devices ? n : 0), DEVICE, n, devices, count);
}

static cl_int info(unsigned kind, const void *object, cl_uint name, size_t size, void *value,
                   size_t *actual, unsigned which, uint64_t extra, cl_int invalid) {
    uint32_t token = id(object, kind);
    if (!token) return invalid;
    if (value && !size) return CL_INVALID_VALUE;
    struct rme_cl_reply reply = exchange(REQUEST(INFO, kind, token, name, value ? size : 0, which, extra),
                                         NULL, 0, 0, NULL, value, value ? size : 0);
    if (!reply.status && actual) *actual = reply.value;
    return reply.status;
}

#define INFO_FN(api, type, param, kind, error, selector) \
    CL_API_ENTRY cl_int CL_API_CALL api(type object, param name, size_t size, void *value, size_t *actual) { \
        return info(kind, object, name, size, value, actual, selector, 0, error); \
    }
INFO_FN(clGetPlatformInfo, cl_platform_id, cl_platform_info, PLATFORM, CL_INVALID_PLATFORM, 0)
INFO_FN(clGetDeviceInfo, cl_device_id, cl_device_info, DEVICE, CL_INVALID_DEVICE, 0)
INFO_FN(clGetContextInfo, cl_context, cl_context_info, CONTEXT, CL_INVALID_CONTEXT, 0)
INFO_FN(clGetCommandQueueInfo, cl_command_queue, cl_command_queue_info, QUEUE, CL_INVALID_COMMAND_QUEUE, 0)
INFO_FN(clGetImageInfo, cl_mem, cl_image_info, MEMORY, CL_INVALID_MEM_OBJECT, INFO_IMAGE)
INFO_FN(clGetKernelInfo, cl_kernel, cl_kernel_info, KERNEL, CL_INVALID_KERNEL, 0)
INFO_FN(clGetEventProfilingInfo, cl_event, cl_profiling_info, EVENT, CL_INVALID_EVENT, 1)
INFO_FN(clGetSamplerInfo, cl_sampler, cl_sampler_info, SAMPLER, CL_INVALID_SAMPLER, 0)
#undef INFO_FN

CL_API_ENTRY cl_int CL_API_CALL clGetEventInfo(cl_event event, cl_event_info name, size_t size, void *value, size_t *actual) {
    pthread_mutex_lock(&wire);
    const struct event_ref *entry = *find_event(id(event, EVENT));
    cl_uint refs = entry ? entry->refs : 0;
    pthread_mutex_unlock(&wire);
    if (!refs) return CL_INVALID_EVENT;
    if (name != CL_EVENT_REFERENCE_COUNT) return info(EVENT, event, name, size, value, actual, 0, 0, CL_INVALID_EVENT);
    if (value && size < sizeof(refs)) return CL_INVALID_VALUE;
    if (actual) *actual = sizeof(refs);
    if (value) memcpy(value, &refs, sizeof(refs));
    return CL_SUCCESS;
}

static struct host_buffer **find_host_buffer(cl_mem memory) {
    struct host_buffer **link = &host_buffers;
    while (*link && (*link)->memory != memory) link = &(*link)->next;
    return link;
}

CL_API_ENTRY cl_int CL_API_CALL clGetMemObjectInfo(cl_mem memory, cl_mem_info name, size_t size, void *value, size_t *actual) {
    if (name == CL_MEM_HOST_PTR || name == CL_MEM_FLAGS || name == CL_MEM_MAP_COUNT) {
        pthread_mutex_lock(&maps_lock);
        const struct host_buffer *buffer = *find_host_buffer(memory);
        if (name == CL_MEM_MAP_COUNT) {
            cl_uint count = 0;
            for (const struct mapping *entry = maps; entry; entry = entry->next) count += entry->memory == memory;
            if (count) {
                if (actual) *actual = sizeof(count);
                if (value && size >= sizeof(count)) memcpy(value, &count, sizeof(count));
                pthread_mutex_unlock(&maps_lock);
                return value && size < sizeof(count) ? CL_INVALID_VALUE : CL_SUCCESS;
            }
        } else if (buffer) {
            size_t need = name == CL_MEM_HOST_PTR ? sizeof(buffer->pointer) : sizeof(buffer->flags);
            if (actual) *actual = need;
            if (value && size >= need) memcpy(value, name == CL_MEM_HOST_PTR ? (const void *)&buffer->pointer
                                                                             : (const void *)&buffer->flags, need);
            pthread_mutex_unlock(&maps_lock);
            return value && size < need ? CL_INVALID_VALUE : CL_SUCCESS;
        }
        pthread_mutex_unlock(&maps_lock);
    }
    return info(MEMORY, memory, name, size, value, actual, 0, 0, CL_INVALID_MEM_OBJECT);
}

CL_API_ENTRY cl_int CL_API_CALL clGetProgramInfo(cl_program program, cl_program_info name, size_t size,
                                                void *value, size_t *actual) {
    if (name != CL_PROGRAM_BINARIES)
        return info(PROGRAM, program, name, size, value, actual, 0, 0, CL_INVALID_PROGRAM);
    size_t bytes = 0;
    cl_int status = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(bytes), &bytes, NULL);
    if (status) return status;
    if (actual) *actual = sizeof(unsigned char *);
    if (!value) return CL_SUCCESS;
    if (size < sizeof(unsigned char *)) return CL_INVALID_VALUE;
    unsigned char *binary = *(unsigned char **)value;
    if (!binary || !bytes) return CL_SUCCESS;
    return exchange(REQUEST(BINARIES, id(program, PROGRAM)), NULL, 0, 0, NULL, binary, bytes).status;
}

CL_API_ENTRY cl_int CL_API_CALL clGetProgramBuildInfo(cl_program program, cl_device_id device,
        cl_program_build_info name, size_t size, void *value, size_t *actual) {
    if (!id(device, DEVICE)) return CL_INVALID_DEVICE;
    return info(PROGRAM, program, name, size, value, actual, INFO_BUILD, id(device, DEVICE), CL_INVALID_PROGRAM);
}

CL_API_ENTRY cl_int CL_API_CALL clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device,
        cl_kernel_work_group_info name, size_t size, void *value, size_t *actual) {
    if (device && !id(device, DEVICE)) return CL_INVALID_DEVICE;
    return info(KERNEL, kernel, name, size, value, actual, INFO_WORKGROUP, id(device, DEVICE), CL_INVALID_KERNEL);
}

CL_API_ENTRY cl_int CL_API_CALL clGetKernelArgInfo(cl_kernel kernel, cl_uint index,
        cl_kernel_arg_info name, size_t size, void *value, size_t *actual) {
    return info(KERNEL, kernel, name, size, value, actual, INFO_ARG, index, CL_INVALID_KERNEL);
}

static cl_int property_platform(const cl_context_properties *properties, cl_platform_id *platform) {
    *platform = NULL;
    for (size_t i = 0; properties && properties[i]; i += 2) {
        if (properties[i] != CL_CONTEXT_PLATFORM || *platform) return CL_INVALID_PROPERTY;
        *platform = (cl_platform_id)properties[i + 1];
        if (!id(*platform, PLATFORM)) return CL_INVALID_PLATFORM;
    }
    return CL_SUCCESS;
}

CL_API_ENTRY cl_context CL_API_CALL clCreateContext(const cl_context_properties *properties, cl_uint count,
        const cl_device_id *devices, void (CL_CALLBACK *notify)(const char *, const void *, size_t, void *),
        void *user, cl_int *error) {
    if (count != 1 || !devices || (!notify && user)) return create_error(CONTEXT, error, CL_INVALID_VALUE);
    if (notify) return create_error(CONTEXT, error, CL_INVALID_OPERATION);
    cl_platform_id platform;
    cl_int status = property_platform(properties, &platform);
    if (status) return create_error(CONTEXT, error, status);
    return create_result(CALL(REQUEST(CREATE_CONTEXT, id(devices[0], DEVICE), id(platform, PLATFORM))), CONTEXT, error);
}

CL_API_ENTRY cl_context CL_API_CALL clCreateContextFromType(const cl_context_properties *properties, cl_device_type type,
        void (CL_CALLBACK *notify)(const char *, const void *, size_t, void *), void *user, cl_int *error) {
    cl_platform_id platform;
    cl_int status = property_platform(properties, &platform);
    if (!status && !platform) status = clGetPlatformIDs(1, &platform, NULL);
    cl_device_id device;
    if (!status) status = clGetDeviceIDs(platform, type, 1, &device, NULL);
    if (status) return create_error(CONTEXT, error, status);
    return clCreateContext(properties, 1, &device, notify, user, error);
}

CL_API_ENTRY cl_command_queue CL_API_CALL clCreateCommandQueue(cl_context context, cl_device_id device,
        cl_command_queue_properties properties, cl_int *error) {
    return create_result(CALL(REQUEST(CREATE_QUEUE, id(context, CONTEXT), id(device, DEVICE), properties)), QUEUE, error);
}

CL_API_ENTRY cl_mem CL_API_CALL clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size,
                                              void *host, cl_int *error) {
    if (!!host != !!(flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)))
        return create_error(MEMORY, error, CL_INVALID_HOST_PTR);
    struct host_buffer *record = NULL;
    if (flags & CL_MEM_USE_HOST_PTR) {
        record = malloc(sizeof(*record));
        if (!record) return create_error(MEMORY, error, CL_OUT_OF_HOST_MEMORY);
    }
    cl_mem buffer = create_result(PAYLOAD(REQUEST(CREATE_BUFFER, id(context, CONTEXT), flags, size), host, host ? size : 0),
                                  MEMORY, error);
    if (record && buffer) {
        pthread_mutex_lock(&maps_lock);
        *record = (struct host_buffer){host_buffers, buffer, host, flags, size, 1};
        host_buffers = record;
        pthread_mutex_unlock(&maps_lock);
    } else free(record);
    return buffer;
}

CL_API_ENTRY cl_mem CL_API_CALL clCreateSubBuffer(cl_mem buffer, cl_mem_flags flags, cl_buffer_create_type type,
                                                 const void *info_value, cl_int *error) {
    if (type != CL_BUFFER_CREATE_TYPE_REGION || !info_value) return create_error(MEMORY, error, CL_INVALID_VALUE);
    const cl_buffer_region *region = info_value;
    return create_result(CALL(REQUEST(CREATE_SUB_BUFFER, id(buffer, MEMORY), flags, region->origin, region->size)),
                         MEMORY, error);
}

CL_API_ENTRY cl_mem CL_API_CALL clCreateImage(cl_context context, cl_mem_flags flags, const cl_image_format *format,
                                             const cl_image_desc *desc, void *host, cl_int *error) {
    if (!format) return create_error(MEMORY, error, CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);
    if (!desc || desc->num_mip_levels || desc->num_samples) return create_error(MEMORY, error, CL_INVALID_IMAGE_DESCRIPTOR);
    if (!!host != !!(flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)))
        return create_error(MEMORY, error, CL_INVALID_HOST_PTR);
    struct rme_cl_request request = REQUEST(CREATE_IMAGE, id(context, CONTEXT), flags, desc->image_type,
        desc->image_width, desc->image_height, desc->image_depth, desc->image_array_size, desc->image_row_pitch,
        desc->image_slice_pitch, id(desc->buffer, MEMORY));
    if (desc->buffer && !request.args[9]) return create_error(MEMORY, error, CL_INVALID_MEM_OBJECT);
    if (!host) return create_result(PAYLOAD(request, format, sizeof(*format)), MEMORY, error);
    size_t region[3];
    descriptor_region(desc, region);
    struct image_layout layout;
    if (!layout_image(&layout, desc->image_type, region, pixel_size(*format), desc->image_row_pitch,
                      desc->image_slice_pitch) || layout.bytes > RME_CL_ABI_MAX - sizeof(*format))
        return create_error(MEMORY, error, CL_INVALID_IMAGE_SIZE);
    unsigned char *data = malloc(sizeof(*format) + layout.bytes);
    if (!data) return create_error(MEMORY, error, CL_OUT_OF_HOST_MEMORY);
    memcpy(data, format, sizeof(*format));
    memcpy(data + sizeof(*format), host, layout.bytes);
    struct rme_cl_reply reply = PAYLOAD(request, data, sizeof(*format) + layout.bytes);
    free(data);
    return create_result(reply, MEMORY, error);
}

CL_API_ENTRY cl_mem CL_API_CALL clCreateImage2D(cl_context context, cl_mem_flags flags, const cl_image_format *format,
        size_t width, size_t height, size_t row, void *host, cl_int *error) {
    cl_image_desc desc = {.image_type = CL_MEM_OBJECT_IMAGE2D, .image_width = width,
                          .image_height = height, .image_row_pitch = row};
    return clCreateImage(context, flags, format, &desc, host, error);
}

CL_API_ENTRY cl_mem CL_API_CALL clCreateImage3D(cl_context context, cl_mem_flags flags, const cl_image_format *format,
        size_t width, size_t height, size_t depth, size_t row, size_t slice, void *host, cl_int *error) {
    cl_image_desc desc = {.image_type = CL_MEM_OBJECT_IMAGE3D, .image_width = width, .image_height = height,
                          .image_depth = depth, .image_row_pitch = row, .image_slice_pitch = slice};
    return clCreateImage(context, flags, format, &desc, host, error);
}

CL_API_ENTRY cl_int CL_API_CALL clGetSupportedImageFormats(cl_context context, cl_mem_flags flags,
        cl_mem_object_type type, cl_uint n, cl_image_format *formats, cl_uint *count) {
    if ((formats && !n) || (!formats && !count)) return CL_INVALID_VALUE;
    cl_uint limit = n < RME_CL_LIST_MAX ? n : RME_CL_LIST_MAX;
    struct rme_cl_reply reply = exchange(REQUEST(IMAGE_FORMATS, id(context, CONTEXT), flags, type, formats ? limit : 0),
                                         NULL, 0, 0, NULL, formats, formats ? limit * sizeof(*formats) : 0);
    if (!reply.status && count) *count = reply.value;
    return reply.status;
}

CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithSource(cl_context context, cl_uint count, const char **strings,
                                                              const size_t *lengths, cl_int *error) {
    if (!count || !strings) return create_error(PROGRAM, error, CL_INVALID_VALUE);
    size_t size = 0;
    for (cl_uint i = 0; i < count; ++i) {
        if (!strings[i]) return create_error(PROGRAM, error, CL_INVALID_VALUE);
        size_t length = lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
        if (length > RME_CL_ABI_MAX - size) return create_error(PROGRAM, error, CL_OUT_OF_RESOURCES);
        size += length;
    }
    if (count == 1) return create_result(PAYLOAD(REQUEST(SOURCE, id(context, CONTEXT)), strings[0], size), PROGRAM, error);
    char *source = malloc(size);
    if (!source) return create_error(PROGRAM, error, CL_OUT_OF_HOST_MEMORY);
    size_t offset = 0;
    for (cl_uint i = 0; i < count; ++i) {
        size_t length = lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
        memcpy(source + offset, strings[i], length);
        offset += length;
    }
    struct rme_cl_reply reply = PAYLOAD(REQUEST(SOURCE, id(context, CONTEXT)), source, size);
    free(source);
    return create_result(reply, PROGRAM, error);
}

CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBinary(cl_context context, cl_uint count,
        const cl_device_id *devices, const size_t *lengths, const unsigned char **binaries,
        cl_int *binary_status, cl_int *error) {
    if (count != 1 || !devices || !lengths || !binaries || !binaries[0] || !lengths[0])
        return create_error(PROGRAM, error, CL_INVALID_VALUE);
    struct rme_cl_reply reply = PAYLOAD(REQUEST(BINARY, id(context, CONTEXT), id(devices[0], DEVICE)), binaries[0], lengths[0]);
    if (binary_status) *binary_status = (cl_int)reply.value;
    return create_result(reply, PROGRAM, error);
}

CL_API_ENTRY cl_int CL_API_CALL clBuildProgram(cl_program program, cl_uint count, const cl_device_id *devices,
        const char *options, void (CL_CALLBACK *notify)(cl_program, void *), void *user) {
    if (count > 1 || (count != 0) != (devices != NULL) || (!notify && user)) return CL_INVALID_VALUE;
    uint32_t device = count ? id(devices[0], DEVICE) : 0;
    if (count && !device) return CL_INVALID_DEVICE;
    size_t size = options ? strlen(options) + 1 : 0;
    struct rme_cl_reply reply = PAYLOAD(REQUEST(BUILD_PROGRAM, id(program, PROGRAM), device, size), options, size);
    if (notify && (!reply.status || reply.status == CL_BUILD_PROGRAM_FAILURE)) notify(program, user);
    return reply.status;
}

static struct rme_cl_reply program_inputs(unsigned op, uint32_t owner, cl_uint count, const cl_device_id *devices,
        const char *options, cl_uint n, const cl_program *inputs, const char **headers) {
    struct rme_cl_reply reply = {.status = CL_INVALID_VALUE};
    if (count > 1 || (count != 0) != (devices != NULL) || n > 32 || (n != 0) != (inputs != NULL) ||
        (op == COMPILE_PROGRAM && (n != 0) != (headers != NULL))) return reply;
    uint32_t device = count ? id(devices[0], DEVICE) : 0;
    if (count && !device) return (struct rme_cl_reply){.status = CL_INVALID_DEVICE};
    size_t option_size = options ? strlen(options) + 1 : 0;
    size_t size = option_size + n * sizeof(uint32_t);
    if (size > RME_CL_ABI_MAX) return (struct rme_cl_reply){.status = CL_OUT_OF_RESOURCES};
    for (cl_uint i = 0; headers && i < n; ++i) {
        if (!headers[i]) return reply;
        size_t length = strlen(headers[i]) + 1;
        if (length > RME_CL_ABI_MAX - size) return (struct rme_cl_reply){.status = CL_OUT_OF_RESOURCES};
        size += length;
    }
    unsigned char *data = size ? malloc(size) : NULL;
    if (size && !data) return (struct rme_cl_reply){.status = CL_OUT_OF_HOST_MEMORY};
    if (option_size) memcpy(data, options, option_size);
    for (cl_uint i = 0; i < n; ++i) {
        uint32_t token = id(inputs[i], PROGRAM);
        memcpy(data + option_size + i * sizeof(token), &token, sizeof(token));
    }
    size_t offset = option_size + n * sizeof(uint32_t);
    for (cl_uint i = 0; headers && i < n; ++i) {
        size_t length = strlen(headers[i]) + 1;
        memcpy(data + offset, headers[i], length);
        offset += length;
    }
    reply = PAYLOAD(REQUEST(op, owner, device, option_size, n), data, size);
    free(data);
    return reply;
}

CL_API_ENTRY cl_int CL_API_CALL clCompileProgram(cl_program program, cl_uint count, const cl_device_id *devices,
        const char *options, cl_uint n, const cl_program *inputs, const char **headers,
        void (CL_CALLBACK *notify)(cl_program, void *), void *user) {
    if (!notify && user) return CL_INVALID_VALUE;
    struct rme_cl_reply reply = program_inputs(COMPILE_PROGRAM, id(program, PROGRAM), count, devices, options, n, inputs, headers);
    if (notify && (!reply.status || reply.status == CL_COMPILE_PROGRAM_FAILURE)) notify(program, user);
    return reply.status;
}

CL_API_ENTRY cl_program CL_API_CALL clLinkProgram(cl_context context, cl_uint count, const cl_device_id *devices,
        const char *options, cl_uint n, const cl_program *inputs, void (CL_CALLBACK *notify)(cl_program, void *),
        void *user, cl_int *error) {
    if (!notify && user) return create_error(PROGRAM, error, CL_INVALID_VALUE);
    struct rme_cl_reply reply = program_inputs(LINK_PROGRAM, id(context, CONTEXT), count, devices, options, n, inputs, NULL);
    cl_program program = create_result(reply, PROGRAM, error);
    if (notify && program) notify(program, user);
    return program;
}

CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithBuiltInKernels(cl_context context, cl_uint count,
        const cl_device_id *devices, const char *names, cl_int *error) {
    if (count != 1 || !devices || !names) return create_error(PROGRAM, error, CL_INVALID_VALUE);
    return create_result(PAYLOAD(REQUEST(BUILTIN_PROGRAM, id(context, CONTEXT), id(devices[0], DEVICE)), names,
                                 strlen(names) + 1), PROGRAM, error);
}

CL_API_ENTRY cl_int CL_API_CALL clUnloadPlatformCompiler(cl_platform_id platform) {
    return CALL(REQUEST(UNLOAD_COMPILER, id(platform, PLATFORM))).status;
}

CL_API_ENTRY cl_kernel CL_API_CALL clCreateKernel(cl_program program, const char *name, cl_int *error) {
    if (!name) return create_error(KERNEL, error, CL_INVALID_VALUE);
    return create_result(PAYLOAD(REQUEST(CREATE_KERNEL, id(program, PROGRAM)), name, strlen(name) + 1), KERNEL, error);
}

CL_API_ENTRY cl_int CL_API_CALL clCreateKernelsInProgram(cl_program program, cl_uint n, cl_kernel *kernels, cl_uint *count) {
    return enumerate(REQUEST(KERNELS, id(program, PROGRAM), kernels ? n : 0), KERNEL, n, kernels, count);
}

CL_API_ENTRY cl_int CL_API_CALL clSetKernelArg(cl_kernel kernel, cl_uint index, size_t size, const void *arg) {
    uint32_t object = 0;
    unsigned kind = 0;
    if (arg && size == sizeof(uintptr_t)) {
        uintptr_t value;
        memcpy(&value, arg, sizeof(value));
        object = id((void *)value, MEMORY);
        if (object) kind = MEMORY;
        else if ((object = id((void *)value, SAMPLER))) kind = SAMPLER;
    }
    return PAYLOAD(REQUEST(SET_ARG, id(kernel, KERNEL), index, size, kind, object), arg, arg && !object ? size : 0).status;
}

static cl_int enqueue(struct rme_cl_request request, const void *data, size_t size, cl_uint count,
                      const cl_event *wait, cl_event *event, void *output, size_t capacity) {
    struct event_ref *entry = event ? malloc(sizeof(*entry)) : NULL;
    if (event && !entry) return CL_OUT_OF_HOST_MEMORY;
    request.event_requested = event != NULL;
    struct rme_cl_reply reply = exchange(request, data, size, count, wait, output, capacity);
    if (reply.status) free(entry);
    else if (entry) {
        pthread_mutex_lock(&wire);
        struct event_ref **bucket = &events[reply.object % EVENT_BUCKETS];
        *entry = (struct event_ref){*bucket, reply.object, 1};
        *bucket = entry;
        pthread_mutex_unlock(&wire);
        *event = handle(reply.object, EVENT);
    }
    return reply.status;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernel(cl_command_queue queue, cl_kernel kernel, cl_uint dims,
        const size_t *offset, const size_t *global, const size_t *local, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!dims || dims > 3) return CL_INVALID_WORK_DIMENSION;
    if (!global) return CL_INVALID_GLOBAL_WORK_SIZE;
    size_t ranges[3][3] = {{0}};
    if (offset) memcpy(ranges[0], offset, dims * sizeof(*offset));
    memcpy(ranges[1], global, dims * sizeof(*global));
    if (local) memcpy(ranges[2], local, dims * sizeof(*local));
    return enqueue(REQUEST(DISPATCH, id(queue, QUEUE), id(kernel, KERNEL), dims, offset != NULL, local != NULL),
                   ranges, sizeof(ranges), count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueTask(cl_command_queue queue, cl_kernel kernel, cl_uint count,
                                            const cl_event *wait, cl_event *event) {
    return enqueue(REQUEST(TASK, id(queue, QUEUE), id(kernel, KERNEL)), NULL, 0, count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueMigrateMemObjects(cl_command_queue queue, cl_uint n, const cl_mem *objects,
        cl_mem_migration_flags flags, cl_uint count, const cl_event *wait, cl_event *event) {
    uint32_t tokens[64];
    if (!objects || !n || n > sizeof(tokens) / sizeof(*tokens)) return CL_INVALID_VALUE;
    for (cl_uint i = 0; i < n; ++i) tokens[i] = id(objects[i], MEMORY);
    return enqueue(REQUEST(MIGRATE, id(queue, QUEUE), flags), tokens, n * sizeof(*tokens), count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        size_t offset, size_t size, void *data, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!data || blocking > CL_TRUE) return CL_INVALID_VALUE;
    struct rme_cl_request request = REQUEST(READ_BUFFER, id(queue, QUEUE), id(memory, MEMORY), offset, size);
    request.args[10] = blocking == CL_FALSE;
    return enqueue(request,
                   NULL, 0, count, wait, event, data, size);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        size_t offset, size_t size, const void *data, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!data || blocking > CL_TRUE) return CL_INVALID_VALUE;
    struct rme_cl_request request = REQUEST(WRITE_BUFFER, id(queue, QUEUE), id(memory, MEMORY), offset);
    request.args[10] = blocking == CL_FALSE;
    return enqueue(request, data, size, count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBuffer(cl_command_queue queue, cl_mem source, cl_mem target,
        size_t source_offset, size_t target_offset, size_t size, cl_uint count, const cl_event *wait, cl_event *event) {
    return enqueue(REQUEST(COPY_BUFFER, id(queue, QUEUE), id(source, MEMORY), source_offset, size, id(target, MEMORY), target_offset),
                   NULL, 0, count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillBuffer(cl_command_queue queue, cl_mem memory, const void *pattern,
        size_t pattern_size, size_t offset, size_t size, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!pattern) return CL_INVALID_VALUE;
    return enqueue(REQUEST(FILL_BUFFER, id(queue, QUEUE), id(memory, MEMORY), offset, size), pattern, pattern_size,
                   count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clFlush(cl_command_queue queue) { return CALL(REQUEST(FLUSH, id(queue, QUEUE))).status; }
CL_API_ENTRY cl_int CL_API_CALL clFinish(cl_command_queue queue) { return CALL(REQUEST(FINISH, id(queue, QUEUE))).status; }

CL_API_ENTRY cl_int CL_API_CALL clWaitForEvents(cl_uint count, const cl_event *events) {
    if (!count || !events) return CL_INVALID_VALUE;
    return exchange(REQUEST(WAIT_EVENTS, 0), NULL, 0, count, events, NULL, 0).status;
}

struct event_callback {
    cl_event event;
    cl_int trigger;
    void (CL_CALLBACK *notify)(cl_event, cl_int, void *);
    void *user;
};

static void *deliver_event(void *opaque) {
    struct event_callback *callback = opaque;
    const struct timespec interval = {.tv_nsec = 1000000};
    cl_int execution;
    for (;;) {
        cl_int status = clGetEventInfo(callback->event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(execution), &execution, NULL);
        if (status) { execution = status; break; }
        if (execution <= callback->trigger) break;
        nanosleep(&interval, NULL);
    }
    callback->notify(callback->event, execution < 0 ? execution : callback->trigger, callback->user);
    clReleaseEvent(callback->event);
    free(callback);
    return NULL;
}

CL_API_ENTRY cl_int CL_API_CALL clSetEventCallback(cl_event event, cl_int trigger,
        void (CL_CALLBACK *notify)(cl_event, cl_int, void *), void *user) {
    if (!notify || trigger < CL_COMPLETE || trigger > CL_SUBMITTED) return CL_INVALID_VALUE;
    struct event_callback *callback = malloc(sizeof(*callback));
    if (!callback) return CL_OUT_OF_HOST_MEMORY;
    cl_int status = clRetainEvent(event);
    if (status) { free(callback); return status; }
    *callback = (struct event_callback){event, trigger, notify, user};
    pthread_t worker;
    if (pthread_create(&worker, NULL, deliver_event, callback)) {
        clReleaseEvent(event);
        free(callback);
        return CL_OUT_OF_HOST_MEMORY;
    }
    pthread_detach(worker);
    return CL_SUCCESS;
}

#define QUEUE_FENCE(api, op) \
    CL_API_ENTRY cl_int CL_API_CALL api(cl_command_queue queue, cl_uint count, const cl_event *wait, cl_event *event) { \
        return enqueue(REQUEST(op, id(queue, QUEUE)), NULL, 0, count, wait, event, NULL, 0); \
    }
QUEUE_FENCE(clEnqueueMarkerWithWaitList, MARKER)
QUEUE_FENCE(clEnqueueBarrierWithWaitList, BARRIER)
#undef QUEUE_FENCE

static struct rme_cl_request image_request(unsigned op, cl_command_queue queue, cl_mem image,
                                            const size_t *origin, const size_t *region) {
    return REQUEST(op, id(queue, QUEUE), id(image, MEMORY), origin[0], origin[1], origin[2], region[0], region[1], region[2]);
}

static cl_int describe_transfer(cl_mem image, const size_t *region, size_t row, size_t slice,
                                struct image_layout *packed, struct image_layout *user) {
    struct rme_cl_reply reply = CALL(REQUEST(IMAGE_LAYOUT, id(image, MEMORY)));
    if (reply.status) return reply.status;
    if (!layout_image(packed, reply.object, region, reply.value, 0, 0) ||
        !layout_image(user, reply.object, region, reply.value, row, slice)) return CL_INVALID_VALUE;
    return CL_SUCCESS;
}

static void copy_rows(void *target, const struct image_layout *dst, const void *source, const struct image_layout *src) {
    for (size_t z = 0; z < src->slices; ++z) {
        for (size_t y = 0; y < src->rows; ++y)
            memcpy((char *)target + z * dst->slice_pitch + y * dst->row_pitch,
                   (const char *)source + z * src->slice_pitch + y * src->row_pitch, src->row);
    }
}

static int host_rect_offset(const struct image_layout *layout, const size_t *origin, size_t *offset) {
    if (origin[0] > RME_CL_ABI_MAX || origin[1] > RME_CL_ABI_MAX / layout->row_pitch ||
        origin[2] > RME_CL_ABI_MAX / layout->slice_pitch) return 0;
    *offset = origin[0] + origin[1] * layout->row_pitch + origin[2] * layout->slice_pitch;
    return *offset <= RME_CL_ABI_MAX - layout->bytes;
}

static cl_int rect_transfer(unsigned op, cl_command_queue queue, cl_mem memory, cl_bool blocking,
        const size_t *origin, const size_t *host_origin, const size_t *region,
        size_t row, size_t slice, size_t host_row, size_t host_slice, void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    if (!origin || !host_origin || !region || !data || blocking > CL_TRUE) return CL_INVALID_VALUE;
    struct image_layout packed, user;
    size_t offset;
    if (!layout_image(&packed, CL_MEM_OBJECT_IMAGE3D, region, 1, 0, 0) ||
        !layout_image(&user, CL_MEM_OBJECT_IMAGE3D, region, 1, host_row, host_slice) ||
        !host_rect_offset(&user, host_origin, &offset)) return CL_INVALID_VALUE;
    data = (char *)data + offset;
    int repack = packed.row_pitch != user.row_pitch || packed.slice_pitch != user.slice_pitch;
    void *payload = repack ? malloc(packed.bytes) : data;
    if (!payload) return CL_OUT_OF_HOST_MEMORY;
    struct rme_cl_request request = image_request(op, queue, memory, origin, region);
    request.args[8] = row;
    request.args[9] = slice;
    cl_int status;
    if (op == WRITE_BUFFER_RECT) {
        request.args[10] = blocking == CL_FALSE;
        if (repack) copy_rows(payload, &packed, data, &user);
        status = enqueue(request, payload, packed.bytes, count, wait, event, NULL, 0);
    } else {
        status = enqueue(request, NULL, 0, count, wait, event, payload, packed.bytes);
        if (!status && repack) copy_rows(data, &user, payload, &packed);
    }
    if (repack) free(payload);
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBufferRect(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        const size_t *origin, const size_t *host_origin, const size_t *region, size_t row, size_t slice,
        size_t host_row, size_t host_slice, void *data, cl_uint count, const cl_event *wait, cl_event *event) {
    return rect_transfer(READ_BUFFER_RECT, queue, memory, blocking, origin, host_origin, region, row, slice,
                         host_row, host_slice, data, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferRect(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        const size_t *origin, const size_t *host_origin, const size_t *region, size_t row, size_t slice,
        size_t host_row, size_t host_slice, const void *data, cl_uint count, const cl_event *wait, cl_event *event) {
    return rect_transfer(WRITE_BUFFER_RECT, queue, memory, blocking, origin, host_origin, region, row, slice,
                         host_row, host_slice, (void *)data, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferRect(cl_command_queue queue, cl_mem source, cl_mem target,
        const size_t *origin, const size_t *target_origin, const size_t *region, size_t row, size_t slice,
        size_t target_row, size_t target_slice, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!origin || !target_origin || !region) return CL_INVALID_VALUE;
    struct rme_cl_request request = image_request(COPY_BUFFER_RECT, queue, source, origin, region);
    request.args[8] = row;
    request.args[9] = slice;
    request.args[10] = id(target, MEMORY);
    size_t target_rect[] = {target_origin[0], target_origin[1], target_origin[2], target_row, target_slice};
    return enqueue(request, target_rect, sizeof(target_rect), count, wait, event, NULL, 0);
}

static cl_int image_transfer(unsigned op, cl_command_queue queue, cl_mem image, cl_bool blocking,
        const size_t *origin, const size_t *region, size_t row, size_t slice, void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    if (!origin || !region || !data || blocking > CL_TRUE) return CL_INVALID_VALUE;
    struct image_layout packed, user;
    cl_int status = describe_transfer(image, region, row, slice, &packed, &user);
    if (status) return status;
    int repack = packed.row_pitch != user.row_pitch || packed.slice_pitch != user.slice_pitch;
    void *payload = repack ? malloc(packed.bytes) : data;
    if (!payload) return CL_OUT_OF_HOST_MEMORY;
    struct rme_cl_request request = image_request(op, queue, image, origin, region);
    if (op == WRITE_IMAGE) {
        request.args[10] = blocking == CL_FALSE;
        if (repack) copy_rows(payload, &packed, data, &user);
        status = enqueue(request, payload, packed.bytes, count, wait, event, NULL, 0);
    } else {
        status = enqueue(request, NULL, 0, count, wait, event, payload, packed.bytes);
        if (!status && repack) copy_rows(data, &user, payload, &packed);
    }
    if (repack) free(payload);
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadImage(cl_command_queue queue, cl_mem image, cl_bool blocking,
        const size_t *origin, const size_t *region, size_t row, size_t slice, void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    return image_transfer(READ_IMAGE, queue, image, blocking, origin, region, row, slice, data, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteImage(cl_command_queue queue, cl_mem image, cl_bool blocking,
        const size_t *origin, const size_t *region, size_t row, size_t slice, const void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    return image_transfer(WRITE_IMAGE, queue, image, blocking, origin, region, row, slice, (void *)data, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImage(cl_command_queue queue, cl_mem source, cl_mem target,
        const size_t *source_origin, const size_t *target_origin, const size_t *region, cl_uint count,
        const cl_event *wait, cl_event *event) {
    if (!source_origin || !target_origin || !region) return CL_INVALID_VALUE;
    struct rme_cl_request request = image_request(COPY_IMAGE, queue, source, source_origin, region);
    request.args[8] = id(target, MEMORY);
    return enqueue(request, target_origin, 3 * sizeof(*target_origin), count, wait, event, NULL, 0);
}

static cl_int image_buffer_copy(unsigned op, cl_command_queue queue, cl_mem image, cl_mem buffer,
        const size_t *origin, const size_t *region, size_t offset, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!origin || !region) return CL_INVALID_VALUE;
    struct rme_cl_request request = image_request(op, queue, image, origin, region);
    request.args[8] = id(buffer, MEMORY);
    request.args[9] = offset;
    return enqueue(request, NULL, 0, count, wait, event, NULL, 0);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImageToBuffer(cl_command_queue queue, cl_mem image, cl_mem buffer,
        const size_t *origin, const size_t *region, size_t offset, cl_uint count, const cl_event *wait, cl_event *event) {
    return image_buffer_copy(IMAGE_TO_BUFFER, queue, image, buffer, origin, region, offset, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferToImage(cl_command_queue queue, cl_mem buffer, cl_mem image,
        size_t offset, const size_t *origin, const size_t *region, cl_uint count, const cl_event *wait, cl_event *event) {
    return image_buffer_copy(BUFFER_TO_IMAGE, queue, image, buffer, origin, region, offset, count, wait, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillImage(cl_command_queue queue, cl_mem image, const void *color,
        const size_t *origin, const size_t *region, cl_uint count, const cl_event *wait, cl_event *event) {
    if (!origin || !region || !color) return CL_INVALID_VALUE;
    return enqueue(image_request(FILL_IMAGE, queue, image, origin, region), color,
                   4 * sizeof(cl_uint), count, wait, event, NULL, 0);
}

static int valid_map(cl_map_flags flags, cl_bool blocking) {
    return blocking <= CL_TRUE && (flags == CL_MAP_WRITE_INVALIDATE_REGION ||
           (flags && !(flags & ~(cl_map_flags)(CL_MAP_READ | CL_MAP_WRITE))));
}

static void *mapping_result(struct mapping *entry, cl_int status, cl_int *error) {
    if (error) *error = status;
    if (status) { if (!entry->borrowed) free(entry->data); free(entry); return NULL; }
    pthread_mutex_lock(&maps_lock);
    entry->next = maps;
    maps = entry;
    pthread_mutex_unlock(&maps_lock);
    return entry->data;
}

static struct mapping *new_mapping(cl_mem memory, cl_map_flags flags, size_t size, size_t offset, int image, cl_int *error) {
    struct mapping *entry = calloc(1, sizeof(*entry));
    if (entry) {
        if (!image) {
            pthread_mutex_lock(&maps_lock);
            const struct host_buffer *record = *find_host_buffer(memory);
            if (record && offset <= record->size && size <= record->size - offset) {
                entry->data = (char *)record->pointer + offset;
                entry->borrowed = 1;
            }
            pthread_mutex_unlock(&maps_lock);
        }
        if (!entry->data) entry->data = malloc(size);
        if (entry->data) {
            entry->memory = memory; entry->flags = flags; entry->size = size; entry->offset = offset; entry->image = image;
            return entry;
        }
    }
    free(entry);
    if (error) *error = CL_OUT_OF_HOST_MEMORY;
    return NULL;
}

CL_API_ENTRY void *CL_API_CALL clEnqueueMapBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        cl_map_flags flags, size_t offset, size_t size, cl_uint count, const cl_event *wait, cl_event *event, cl_int *error) {
    if (!valid_map(flags, blocking) || !size || size > RME_CL_ABI_MAX) return create_error(MEMORY, error, CL_INVALID_VALUE);
    if (flags == CL_MAP_WRITE_INVALIDATE_REGION) {
        size_t buffer_size;
        cl_int status = clGetMemObjectInfo(memory, CL_MEM_SIZE, sizeof(buffer_size), &buffer_size, NULL);
        if (status) return create_error(MEMORY, error, status);
        if (offset > buffer_size || size > buffer_size - offset) return create_error(MEMORY, error, CL_INVALID_VALUE);
    }
    struct mapping *entry = new_mapping(memory, flags, size, offset, 0, error);
    if (!entry) return NULL;
    cl_int status;
    if (flags == CL_MAP_WRITE_INVALIDATE_REGION) {
        cl_event marker;
        status = clEnqueueMarkerWithWaitList(queue, count, wait, &marker);
        if (!status) {
            status = clWaitForEvents(1, &marker);
            if (!status && event) *event = marker;
            else clReleaseEvent(marker);
        }
    } else {
        status = clEnqueueReadBuffer(queue, memory, CL_TRUE, offset, size, entry->data, count, wait, event);
    }
    return mapping_result(entry, status, error);
}

CL_API_ENTRY void *CL_API_CALL clEnqueueMapImage(cl_command_queue queue, cl_mem image, cl_bool blocking,
        cl_map_flags flags, const size_t *origin, const size_t *region, size_t *row, size_t *slice,
        cl_uint count, const cl_event *wait, cl_event *event, cl_int *error) {
    if (!valid_map(flags, blocking) || !origin || !region || !row) return create_error(MEMORY, error, CL_INVALID_VALUE);
    struct image_layout packed, ignored;
    cl_int status = describe_transfer(image, region, 0, 0, &packed, &ignored);
    if (status) return create_error(MEMORY, error, status);
    struct mapping *entry = new_mapping(image, flags, packed.bytes, 0, 1, error);
    if (!entry) return NULL;
    memcpy(entry->origin, origin, sizeof(entry->origin));
    memcpy(entry->region, region, sizeof(entry->region));
    status = enqueue(image_request(READ_IMAGE, queue, image, origin, region), NULL, 0, count, wait, event,
                     entry->data, packed.bytes);
    if (!status) { *row = packed.row_pitch; if (slice) *slice = packed.slice_pitch; }
    return mapping_result(entry, status, error);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueUnmapMemObject(cl_command_queue queue, cl_mem memory, void *pointer,
        cl_uint count, const cl_event *wait, cl_event *event) {
    pthread_mutex_lock(&maps_lock);
    struct mapping **link = &maps;
    while (*link && ((*link)->data != pointer || (*link)->memory != memory)) link = &(*link)->next;
    struct mapping *entry = *link;
    cl_int status = CL_INVALID_VALUE;
    if (entry) {
        if (!(entry->flags & (CL_MAP_WRITE | CL_MAP_WRITE_INVALIDATE_REGION))) {
            status = clEnqueueMarkerWithWaitList(queue, count, wait, event);
        } else if (entry->image) {
            status = enqueue(image_request(WRITE_IMAGE, queue, memory, entry->origin, entry->region),
                             entry->data, entry->size, count, wait, event, NULL, 0);
        } else {
            status = clEnqueueWriteBuffer(queue, memory, CL_TRUE, entry->offset, entry->size, entry->data, count, wait, event);
        }
        if (!status) { *link = entry->next; if (!entry->borrowed) free(entry->data); free(entry); }
    }
    pthread_mutex_unlock(&maps_lock);
    return status;
}

CL_API_ENTRY cl_sampler CL_API_CALL clCreateSampler(cl_context context, cl_bool normalized, cl_addressing_mode address,
                                                    cl_filter_mode filter, cl_int *error) {
    return create_result(CALL(REQUEST(CREATE_SAMPLER, id(context, CONTEXT), normalized, address, filter)), SAMPLER, error);
}

#define REF_FN(api, type, kind, op, error) \
    CL_API_ENTRY cl_int CL_API_CALL api(type object) { \
        uint32_t token = id(object, kind); \
        return token ? CALL(REQUEST(op, kind, token)).status : error; \
    }
#define REFS(name, type, kind, error) \
    REF_FN(clRetain##name, type, kind, RETAIN, error) \
    REF_FN(clRelease##name, type, kind, RELEASE, error)
REFS(Context, cl_context, CONTEXT, CL_INVALID_CONTEXT)
REFS(CommandQueue, cl_command_queue, QUEUE, CL_INVALID_COMMAND_QUEUE)
REFS(Program, cl_program, PROGRAM, CL_INVALID_PROGRAM)
REFS(Kernel, cl_kernel, KERNEL, CL_INVALID_KERNEL)
REFS(Sampler, cl_sampler, SAMPLER, CL_INVALID_SAMPLER)
#undef REFS
#undef REF_FN

CL_API_ENTRY cl_int CL_API_CALL clRetainEvent(cl_event event) {
    pthread_mutex_lock(&wire);
    struct event_ref *entry = *find_event(id(event, EVENT));
    cl_int status = entry ? CL_SUCCESS : CL_INVALID_EVENT;
    if (entry) ++entry->refs;
    pthread_mutex_unlock(&wire);
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clReleaseEvent(cl_event event) {
    pthread_mutex_lock(&wire);
    struct event_ref **link = find_event(id(event, EVENT));
    struct event_ref *entry = *link;
    if (!entry) { pthread_mutex_unlock(&wire); return CL_INVALID_EVENT; }
    cl_int status = CL_SUCCESS;
    if (!--entry->refs) {
        uint32_t send = entry->id;
        if (peer_caps & RME_CL_CAP_INLINE_RELEASE) {
            // The next request can retire this event before it reaches the driver.
            send = pending_release;
            pending_release = entry->id;
        }
        struct rme_cl_request request = REQUEST(RELEASE | RME_CL_ASYNC_RELEASE, EVENT, send);
        request.magic = RME_CL_ABI_MAGIC;
        *link = entry->next;
        free(entry);
        if (connection < 0 || (send && rme_cl_write_parts(connection, &request, sizeof(request), NULL, 0, NULL, 0))) {
            if (connection >= 0) disconnect();
            status = CL_DEVICE_NOT_AVAILABLE;
        }
    }
    pthread_mutex_unlock(&wire);
    return status;
}

static cl_int memory_ref(cl_mem memory, int retain) {
    uint32_t token = id(memory, MEMORY);
    if (!token) return CL_INVALID_MEM_OBJECT;
    pthread_mutex_lock(&maps_lock);
    cl_int status = CALL(REQUEST(retain ? RETAIN : RELEASE, MEMORY, token)).status;
    struct host_buffer **link = find_host_buffer(memory);
    if (!status && *link) {
        if (retain) ++(*link)->refs;
        else if (!--(*link)->refs) {
            struct host_buffer *old = *link;
            *link = old->next;
            free(old);
        }
    }
    pthread_mutex_unlock(&maps_lock);
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clRetainMemObject(cl_mem memory) { return memory_ref(memory, 1); }
CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObject(cl_mem memory) { return memory_ref(memory, 0); }

CL_API_ENTRY cl_int CL_API_CALL clRetainDevice(cl_device_id device) { return id(device, DEVICE) ? CL_SUCCESS : CL_INVALID_DEVICE; }
CL_API_ENTRY cl_int CL_API_CALL clReleaseDevice(cl_device_id device) { return clRetainDevice(device); }

/* API loaders can resolve these entry points. The bridge has no device
 * partitioning or cross-process implementation of these operations. */
CL_API_ENTRY cl_int CL_API_CALL clCreateSubDevices(cl_device_id device, const cl_device_partition_property *properties,
        cl_uint n, cl_device_id *devices, cl_uint *count) {
    (void)device; (void)properties; (void)n; (void)devices; (void)count;
    return CL_INVALID_OPERATION;
}

CL_API_ENTRY cl_event CL_API_CALL clCreateUserEvent(cl_context context, cl_int *error) {
    (void)context;
    return create_error(EVENT, error, CL_INVALID_OPERATION);
}

CL_API_ENTRY cl_int CL_API_CALL clSetUserEventStatus(cl_event event, cl_int status) {
    (void)event; (void)status;
    return CL_INVALID_OPERATION;
}

CL_API_ENTRY cl_int CL_API_CALL clSetMemObjectDestructorCallback(cl_mem memory,
        void (CL_CALLBACK *notify)(cl_mem, void *), void *user) {
    (void)memory; (void)notify; (void)user;
    return CL_INVALID_OPERATION;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueNativeKernel(cl_command_queue queue, void (CL_CALLBACK *code)(void *),
        void *args, size_t bytes, cl_uint memory_count, const cl_mem *memory, const void **locations,
        cl_uint count, const cl_event *wait, cl_event *event) {
    (void)queue; (void)code; (void)args; (void)bytes; (void)memory_count; (void)memory;
    (void)locations; (void)count; (void)wait; (void)event;
    return CL_INVALID_OPERATION;
}

CL_API_ENTRY void *CL_API_CALL clGetExtensionFunctionAddressForPlatform(cl_platform_id platform, const char *name) {
    (void)platform;
    (void)name;
    return NULL;
}
