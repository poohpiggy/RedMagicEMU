#ifdef RME_CL_PROFILE
#define _POSIX_C_SOURCE 200809L
#endif
#include "abi.h"

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include "abi_image.h"
#ifdef _WIN32
/* Windows verification uses the NDK compiler and the system CRT import library. */
void *realloc(void *, size_t);
void *malloc(size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
#else
#include <stdlib.h>
#include <string.h>
#endif

#ifdef RME_CL_PROFILE
#include <android/log.h>
#include <time.h>

struct call_metrics {
    uint64_t calls, input_bytes, output_bytes, input_ns, driver_ns, output_ns, nonblocking, shared_bytes;
};

static uint64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
#endif

#define FUNCTIONS(X) \
    X(clGetPlatformIDs) X(clGetPlatformInfo) X(clGetDeviceIDs) X(clGetDeviceInfo) \
    X(clCreateContext) X(clGetContextInfo) X(clRetainContext) X(clReleaseContext) \
    X(clCreateCommandQueue) X(clGetCommandQueueInfo) X(clRetainCommandQueue) X(clReleaseCommandQueue) \
    X(clCreateBuffer) X(clCreateSubBuffer) X(clGetMemObjectInfo) X(clRetainMemObject) X(clReleaseMemObject) \
    X(clCreateProgramWithSource) X(clCreateProgramWithBinary) X(clBuildProgram) X(clGetProgramInfo) \
    X(clCreateProgramWithBuiltInKernels) X(clCompileProgram) X(clLinkProgram) X(clUnloadPlatformCompiler) \
    X(clGetProgramBuildInfo) X(clRetainProgram) X(clReleaseProgram) \
    X(clCreateKernel) X(clCreateKernelsInProgram) X(clSetKernelArg) X(clGetKernelInfo) X(clGetKernelArgInfo) X(clGetKernelWorkGroupInfo) \
    X(clRetainKernel) X(clReleaseKernel) X(clEnqueueNDRangeKernel) \
    X(clEnqueueReadBuffer) X(clEnqueueWriteBuffer) X(clEnqueueCopyBuffer) X(clEnqueueFillBuffer) X(clEnqueueTask) \
    X(clEnqueueReadBufferRect) X(clEnqueueWriteBufferRect) X(clEnqueueCopyBufferRect) X(clEnqueueMigrateMemObjects) \
    X(clWaitForEvents) X(clGetEventInfo) X(clGetEventProfilingInfo) X(clRetainEvent) X(clReleaseEvent) X(clSetEventCallback) \
    X(clEnqueueMarkerWithWaitList) X(clEnqueueBarrierWithWaitList) X(clFlush) X(clFinish) \
    X(clCreateImage) X(clGetSupportedImageFormats) X(clGetImageInfo) X(clEnqueueReadImage) \
    X(clEnqueueWriteImage) X(clEnqueueCopyImage) X(clEnqueueCopyBufferToImage) X(clEnqueueCopyImageToBuffer) \
    X(clEnqueueFillImage) X(clCreateSampler) X(clGetSamplerInfo) X(clRetainSampler) X(clReleaseSampler)

#define FIELD(name) __typeof__(&name) name;
struct api { FUNCTIONS(FIELD) };
#undef FIELD

struct object { void *native; uint32_t kind, refs; };
struct bytes { unsigned char *data; size_t capacity; int fixed; };
struct upload_budget { size_t bytes; unsigned refs; };
struct upload {
    struct upload_budget *budget;
    size_t size;
    unsigned char data[];
};

struct pending_read {
    struct pending_read *next;
    cl_event event;
    uint64_t ticket;
    size_t size;
    cl_int execution;
    struct rme_cl_readback *region;
    unsigned slot;
    unsigned char data[];
};

struct session {
    struct api api;
    struct object *objects;
    uint32_t count, capacity;
    struct bytes input, output;
    struct upload_budget *uploads;
    struct pending_read *reads;
    size_t read_bytes;
    struct rme_cl_readback *readback;
    uint32_t capabilities;
};

static void release_upload_budget(struct upload_budget *budget) {
    if (__atomic_fetch_sub(&budget->refs, 1, __ATOMIC_ACQ_REL) == 1) free(budget);
}

static void CL_CALLBACK upload_done(cl_event event, cl_int status, void *opaque) {
    (void)event;
    (void)status;
    struct upload *upload = opaque;
    struct upload_budget *budget = upload->budget;
    __atomic_fetch_sub(&budget->bytes, upload->size, __ATOMIC_RELAXED);
    free(upload);
    release_upload_budget(budget);
}

static struct upload *stage_upload(struct session *s, const void *data, size_t size) {
    if (!s->uploads) {
        s->uploads = malloc(sizeof(*s->uploads));
        if (!s->uploads) return NULL;
        *s->uploads = (struct upload_budget){.refs = 1};
    }
    if (size > RME_CL_ABI_MAX - __atomic_load_n(&s->uploads->bytes, __ATOMIC_RELAXED)) return NULL;
    struct upload *upload = malloc(sizeof(*upload) + size);
    if (!upload) return NULL;
    upload->budget = s->uploads;
    upload->size = size;
    memcpy(upload->data, data, size);
    __atomic_fetch_add(&s->uploads->refs, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s->uploads->bytes, size, __ATOMIC_RELAXED);
    return upload;
}

static cl_int track_upload(struct api *api, cl_command_queue queue, struct upload *upload,
        cl_int status, cl_event event, cl_event *out) {
    if (status) {
        upload_done(NULL, status, upload);
        return status;
    }
    status = api->clSetEventCallback(event, CL_COMPLETE, upload_done, upload);
    if (status) {
        // If callback registration fails, only reclaim storage after the driver reaches a terminal state.
        status = api->clWaitForEvents(1, &event);
        cl_int execution;
        if (!status || status == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST ||
            (!api->clGetEventInfo(event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(execution), &execution, NULL) &&
             execution <= CL_COMPLETE) || !api->clFinish(queue)) upload_done(event, status, upload);
    }
    if (!status && out) *out = event;
    else api->clReleaseEvent(event);
    return status;
}

static cl_int write_buffer(struct session *s, cl_command_queue queue, cl_mem buffer, size_t offset,
        size_t bytes, const void *data, cl_uint count, const cl_event *wait, cl_event *out, int nonblocking) {
    struct api *api = &s->api;
    struct upload *upload = nonblocking ? stage_upload(s, data, bytes) : NULL;
    if (!upload) return api->clEnqueueWriteBuffer(queue, buffer, CL_TRUE, offset, bytes, data, count, wait, out);

    cl_event event = NULL;
    cl_int status = api->clEnqueueWriteBuffer(queue, buffer, CL_FALSE, offset, bytes, upload->data, count, wait, &event);
    return track_upload(api, queue, upload, status, event, out);
}

static cl_int write_image(struct session *s, cl_command_queue queue, cl_mem image, const size_t *origin,
        const size_t *region, size_t bytes, const void *data, cl_uint count, const cl_event *wait,
        cl_event *out, int nonblocking) {
    struct api *api = &s->api;
    if (!nonblocking)
        return api->clEnqueueWriteImage(queue, image, CL_TRUE, origin, region, 0, 0, data, count, wait, out);

    struct upload *upload = stage_upload(s, data, bytes);
    if (!upload) return CL_OUT_OF_HOST_MEMORY;

    cl_event event = NULL;
    cl_int status = api->clEnqueueWriteImage(queue, image, CL_FALSE, origin, region, 0, 0,
                                             upload->data, count, wait, &event);
    return track_upload(api, queue, upload, status, event, out);
}

static int load_api(struct api *api, void *driver, rme_cl_abi_resolve resolve) {
#define LOAD(name) api->name = (__typeof__(&name))resolve(driver, #name); if (!api->name) return CL_INVALID_OPERATION;
    FUNCTIONS(LOAD)
#undef LOAD
    return CL_SUCCESS;
}

int rme_cl_abi_check(void *driver, rme_cl_abi_resolve resolve) {
    struct api api;
    cl_int status = load_api(&api, driver, resolve);
    if (status) return status;
    enum { probe_limit = 32 };
    cl_platform_id platforms[probe_limit];
    cl_uint count = 0;
    status = api.clGetPlatformIDs(probe_limit, platforms, &count);
    if (status) return status;
    for (cl_uint i = 0; i < count && i < probe_limit; ++i) {
        cl_device_id devices[probe_limit];
        cl_uint found = 0;
        if (api.clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, probe_limit, devices, &found)) continue;
        for (cl_uint j = 0; j < found && j < probe_limit; ++j) {
            cl_bool available = CL_FALSE, compiler = CL_FALSE;
            if (!api.clGetDeviceInfo(devices[j], CL_DEVICE_AVAILABLE, sizeof(available), &available, NULL) && available &&
                !api.clGetDeviceInfo(devices[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof(compiler), &compiler, NULL) && compiler)
                return CL_SUCCESS;
        }
    }
    return CL_DEVICE_NOT_AVAILABLE;
}

static void *reserve(struct bytes *buffer, size_t size) {
    if (!size) return NULL;
    if (size > RME_CL_ABI_MAX) return NULL;
    if (size > buffer->capacity) {
        if (buffer->fixed) return NULL;
        unsigned char *data = realloc(buffer->data, size);
        if (!data) return NULL;
        buffer->data = data;
        buffer->capacity = size;
    }
    return buffer->data;
}

static void *object(struct session *s, uint64_t id, unsigned kind) {
    return id && id <= s->count && s->objects[id - 1].kind == kind ? s->objects[id - 1].native : NULL;
}

static uint32_t remember(struct session *s, void *native, unsigned kind, unsigned owned) {
    if (!native) return 0;
    if (!owned) {
        for (uint32_t i = 0; i < s->count; ++i) {
            if (s->objects[i].native == native && s->objects[i].kind == kind) return i + 1;
        }
    }
    if (s->count == s->capacity) {
        uint32_t capacity = s->capacity ? s->capacity * 2 : 64;
        if (capacity <= s->capacity) return 0;
        struct object *objects = realloc(s->objects, capacity * sizeof(*objects));
        if (!objects) return 0;
        s->objects = objects;
        s->capacity = capacity;
    }
    s->objects[s->count++] = (struct object){native, kind, owned};
    return s->count;
}

static cl_int reference(struct api *api, void *native, unsigned kind, int retain) {
#define CASE(type, cltype) case type: return retain ? api->clRetain##cltype(native) : api->clRelease##cltype(native);
    switch (kind) {
    CASE(CONTEXT, Context)
    CASE(QUEUE, CommandQueue)
    CASE(MEMORY, MemObject)
    CASE(PROGRAM, Program)
    CASE(KERNEL, Kernel)
    CASE(EVENT, Event)
    CASE(SAMPLER, Sampler)
    default: return CL_INVALID_VALUE;
    }
#undef CASE
}

static cl_int change_reference(struct session *s, uint64_t id, unsigned kind, int retain) {
    void *native = object(s, id, kind);
    if (!native) return CL_INVALID_VALUE;
    struct object *entry = s->objects + id - 1;
    if (!retain && !entry->refs) return CL_INVALID_VALUE;
    cl_int status = reference(&s->api, native, kind, retain);
    if (!status) {
        if (retain) ++entry->refs;
        else if (!--entry->refs) entry->native = NULL;
    }
    return status;
}

static cl_int created(struct session *s, struct rme_cl_reply *reply, void *native, unsigned kind) {
    reply->object = remember(s, native, kind, 1);
    if (reply->object) return CL_SUCCESS;
    if (native) reference(&s->api, native, kind, 0);
    return CL_OUT_OF_HOST_MEMORY;
}

static void release_readback(struct rme_cl_readback *region) {
    if (__atomic_fetch_sub(&region->refs, 1, __ATOMIC_ACQ_REL) == 1) region->destroy(region);
}

static unsigned char *read_destination(struct pending_read *read) {
    return read->region ? read->region->data + (size_t)read->slot * RME_CL_READBACK_SLOT_SIZE : read->data;
}

static void free_read(struct pending_read *read) {
    if (read->region) {
        __atomic_fetch_and(&read->region->occupied, ~(1u << read->slot), __ATOMIC_RELEASE);
        release_readback(read->region);
    }
    free(read);
}

static void CL_CALLBACK discard_read(cl_event event, cl_int status, void *opaque) {
    (void)event;
    (void)status;
    free_read(opaque);
}

static void retire_read(struct api *api, struct pending_read *read) {
    cl_event event = read->event;
    if (api->clSetEventCallback(event, CL_COMPLETE, discard_read, read)) {
        cl_int status = api->clWaitForEvents(1, &event);
        if (!status || status == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST) free_read(read);
    }
    api->clReleaseEvent(event);
}

static cl_int submit_read(struct session *s, const struct rme_cl_request *request, struct rme_cl_reply *reply,
        cl_command_queue queue, cl_mem buffer, cl_uint count, const cl_event *wait) {
    const uint64_t *args = request->args;
    size_t size = args[3];
    if (!size || !args[4]) return CL_INVALID_VALUE;
    if (size > RME_CL_ABI_MAX - s->read_bytes) return CL_OUT_OF_RESOURCES;
    struct rme_cl_readback *region = args[5] && size >= RME_CL_SHARED_MIN && size <= RME_CL_READBACK_SLOT_SIZE
        ? s->readback : NULL;
    unsigned slot = 0;
    if (region) {
        unsigned occupied = __atomic_load_n(&region->occupied, __ATOMIC_ACQUIRE);
        while (slot < RME_CL_READBACK_SLOT_COUNT && (occupied & (1u << slot))) ++slot;
        if (slot == RME_CL_READBACK_SLOT_COUNT) region = NULL;
    }
    struct pending_read *read = malloc(sizeof(*read) + (region ? 0 : size));
    if (!read) return CL_OUT_OF_HOST_MEMORY;
    *read = (struct pending_read){.ticket = args[4], .size = size, .region = region, .slot = slot};
    if (region) {
        __atomic_fetch_add(&region->refs, 1, __ATOMIC_RELAXED);
        __atomic_fetch_or(&region->occupied, 1u << slot, __ATOMIC_RELAXED);
    }

    struct api *api = &s->api;
    cl_int status = api->clEnqueueReadBuffer(queue, buffer, CL_FALSE, args[2], size, read_destination(read),
                                            count, wait, &read->event);
    if (status) {
        free_read(read);
        return status;
    }
    if (request->event_requested) {
        status = api->clRetainEvent(read->event);
        if (!status) status = created(s, reply, read->event, EVENT);
        if (status) {
            retire_read(api, read);
            return status;
        }
    }
    read->next = s->reads;
    s->reads = read;
    s->read_bytes += size;
    return CL_SUCCESS;
}

static int complete_reads(struct session *s, void *stream, rme_cl_abi_write write, const struct bytes *mapping) {
    int status = 0;
    struct pending_read *ready = NULL;
    struct pending_read **link = &s->reads;
    /* Snapshot newer events first. A completed dependent read implies an
     * earlier read has completed too. Publish the ready reads in submission order. */
    while (*link) {
        struct pending_read *read = *link;
        if (s->api.clGetEventInfo(read->event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                sizeof(read->execution), &read->execution, NULL)) {
            status = -1;
            break;
        }
        if (read->execution > CL_COMPLETE) {
            link = &read->next;
        } else {
            *link = read->next;
            read->next = ready;
            ready = read;
        }
    }
    while (ready) {
        struct pending_read *read = ready;
        ready = ready->next;
        size_t bytes = read->execution == CL_COMPLETE ? read->size : 0;
        struct rme_cl_reply reply = {.magic = RME_CL_READ_READY_MAGIC, .status = read->execution,
                                     .bytes = (uint32_t)bytes, .value = read->ticket};
        if (!status) {
            const unsigned char *data = read_destination(read);
            if (bytes && read->region) {
                reply.object = RME_CL_READBACK_FIRST_SLOT + read->slot;
            } else if (bytes >= RME_CL_SHARED_MIN && mapping && bytes <= mapping->capacity) {
                /* Use the regular mapping for at most one completion. */
                memcpy(mapping->data, data, bytes);
                mapping = NULL;
                reply.object = 1;
            }
            if (write(stream, &reply, data, reply.object ? 0 : bytes)) status = -1;
        }
        s->read_bytes -= read->size;
        s->api.clReleaseEvent(read->event);
        /* The Guest consumes all completions before sending another request. */
        free_read(read);
    }
    return status;
}

static int terminated(const unsigned char *data, size_t bytes) { return bytes && !data[bytes - 1]; }

static cl_int enumerate(struct session *s, struct rme_cl_reply *reply, unsigned kind,
                        cl_platform_id platform, cl_device_type type, uint64_t limit) {
    cl_uint count = 0;
    union { cl_platform_id platforms[RME_CL_LIST_MAX]; cl_device_id devices[RME_CL_LIST_MAX]; } list;
    cl_uint capacity = limit > RME_CL_LIST_MAX ? RME_CL_LIST_MAX : (cl_uint)limit;
    cl_int status = kind == PLATFORM
        ? s->api.clGetPlatformIDs(capacity, capacity ? list.platforms : NULL, &count)
        : s->api.clGetDeviceIDs(platform, type, capacity, capacity ? list.devices : NULL, &count);
    if (status) return status;
    reply->value = count;
    if (count > capacity) count = capacity;
    if (!count) return CL_SUCCESS;
    uint32_t *ids = reserve(&s->output, count * sizeof(*ids));
    if (!ids) return CL_OUT_OF_HOST_MEMORY;
    for (cl_uint i = 0; i < count; ++i) {
        ids[i] = remember(s, kind == PLATFORM ? (void *)list.platforms[i] : list.devices[i], kind, 0);
        if (!ids[i]) return CL_OUT_OF_HOST_MEMORY;
    }
    reply->bytes = count * sizeof(*ids);
    return CL_SUCCESS;
}

static unsigned info_handle(unsigned kind, unsigned which, cl_uint param) {
    if (kind == DEVICE && param == CL_DEVICE_PLATFORM) return PLATFORM;
    if (kind == DEVICE && param == CL_DEVICE_PARENT_DEVICE) return DEVICE;
    if (kind == CONTEXT && param == CL_CONTEXT_DEVICES) return DEVICE;
    if (kind == QUEUE && param == CL_QUEUE_CONTEXT) return CONTEXT;
    if (kind == QUEUE && param == CL_QUEUE_DEVICE) return DEVICE;
    if (kind == MEMORY && which == INFO_IMAGE && param == CL_IMAGE_BUFFER) return MEMORY;
    if (kind == MEMORY && !which && param == CL_MEM_CONTEXT) return CONTEXT;
    if (kind == MEMORY && !which && param == CL_MEM_ASSOCIATED_MEMOBJECT) return MEMORY;
    if (kind == PROGRAM && !which && param == CL_PROGRAM_CONTEXT) return CONTEXT;
    if (kind == PROGRAM && !which && param == CL_PROGRAM_DEVICES) return DEVICE;
    if (kind == KERNEL && !which && param == CL_KERNEL_CONTEXT) return CONTEXT;
    if (kind == KERNEL && !which && param == CL_KERNEL_PROGRAM) return PROGRAM;
    if (kind == EVENT && !which && param == CL_EVENT_CONTEXT) return CONTEXT;
    if (kind == EVENT && !which && param == CL_EVENT_COMMAND_QUEUE) return QUEUE;
    if (kind == SAMPLER && param == CL_SAMPLER_CONTEXT) return CONTEXT;
    return 0;
}

static cl_int device_extensions(struct session *s, cl_device_id device, void *output, size_t limit, size_t *size) {
    static const char fp16[] = CL_KHR_FP16_EXTENSION_NAME;
    size_t length;
    cl_int status = s->api.clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, NULL, &length);
    if (status) return status;
    char *extensions = reserve(&s->input, length);
    if (!extensions) return CL_OUT_OF_HOST_MEMORY;
    status = s->api.clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, length, extensions, NULL);
    if (status) return status;

    /* FP16 adds compiler types and a scalar capability query to the existing ABI. */
    const char *advertised = "";
    *size = 1;
    for (size_t i = 0; i + sizeof(fp16) <= length; ++i) {
        if (i && extensions[i - 1] != ' ') continue;
        char end = extensions[i + sizeof(fp16) - 1];
        if ((end == ' ' || end == '\0') && !memcmp(extensions + i, fp16, sizeof(fp16) - 1)) {
            advertised = fp16;
            *size = sizeof(fp16);
            break;
        }
    }
    if (output) {
        if (limit < *size) return CL_INVALID_VALUE;
        memcpy(output, advertised, *size);
    }
    return CL_SUCCESS;
}

static cl_int query(struct session *s, const uint64_t *a, struct rme_cl_reply *reply) {
    unsigned kind = a[0], which = a[4];
    void *native = object(s, a[1], kind);
    if (!native) return CL_INVALID_VALUE;
    cl_uint name = a[2];
    size_t limit = a[3], size = 0;
    void *output = reserve(&s->output, limit);
    if (limit && !output) return CL_OUT_OF_HOST_MEMORY;
    cl_int status;
    struct api *api = &s->api;
    /* A plain guest pointer or driver-private structure cannot be copied across this ABI. */
    if ((kind == DEVICE && (name < CL_DEVICE_TYPE || name > CL_DEVICE_PRINTF_BUFFER_SIZE)) ||
        (kind == PROGRAM && !which && name == CL_PROGRAM_BINARIES)) return CL_INVALID_VALUE;
    if (kind == DEVICE && name == CL_DEVICE_EXTENSIONS) {
        status = device_extensions(s, native, output, limit, &size);
    } else if (kind == PLATFORM && name == CL_PLATFORM_EXTENSIONS) {
        size = 1;
        if (limit) ((char *)output)[0] = 0;
        status = CL_SUCCESS;
    } else if ((kind == DEVICE && name == CL_DEVICE_VERSION) || (kind == PLATFORM && name == CL_PLATFORM_VERSION)) {
        static const char version[] = "OpenCL 1.2 RedMagicEMU";
        size = sizeof(version);
        if (limit && limit < size) return CL_INVALID_VALUE;
        if (limit) memcpy(output, version, size);
        status = CL_SUCCESS;
    } else {
        switch (kind) {
        case PLATFORM: status = api->clGetPlatformInfo(native, name, limit, output, &size); break;
        case DEVICE: status = api->clGetDeviceInfo(native, name, limit, output, &size); break;
        case CONTEXT: status = api->clGetContextInfo(native, name, limit, output, &size); break;
        case QUEUE: status = api->clGetCommandQueueInfo(native, name, limit, output, &size); break;
        case MEMORY:
            status = which == INFO_IMAGE ? api->clGetImageInfo(native, name, limit, output, &size)
                                         : api->clGetMemObjectInfo(native, name, limit, output, &size);
            break;
        case PROGRAM:
            status = which == INFO_BUILD ? api->clGetProgramBuildInfo(native, object(s, a[5], DEVICE), name, limit, output, &size)
                                         : api->clGetProgramInfo(native, name, limit, output, &size);
            break;
        case KERNEL:
            if (which == INFO_WORKGROUP)
                status = api->clGetKernelWorkGroupInfo(native, object(s, a[5], DEVICE), name, limit, output, &size);
            else if (which == INFO_ARG)
                status = api->clGetKernelArgInfo(native, a[5], name, limit, output, &size);
            else status = api->clGetKernelInfo(native, name, limit, output, &size);
            break;
        case EVENT:
            status = which ? api->clGetEventProfilingInfo(native, name, limit, output, &size)
                           : api->clGetEventInfo(native, name, limit, output, &size);
            break;
        case SAMPLER: status = api->clGetSamplerInfo(native, name, limit, output, &size); break;
        default: return CL_INVALID_VALUE;
        }
    }
    if (status) return status;
    if (output && size > limit) return CL_INVALID_VALUE;
    unsigned embedded = info_handle(kind, which, name);
    if (output && embedded) {
        for (size_t offset = 0; offset + sizeof(void *) <= size; offset += sizeof(void *)) {
            void *pointer;
            memcpy(&pointer, (char *)output + offset, sizeof(pointer));
            uint32_t id = remember(s, pointer, embedded, 0);
            if (pointer && !id) return CL_OUT_OF_HOST_MEMORY;
            uint64_t handle = rme_cl_handle(id, embedded);
            memcpy((char *)output + offset, &handle, sizeof(handle));
        }
    }
    if (output && kind == CONTEXT && name == CL_CONTEXT_PROPERTIES) {
        intptr_t *properties = output;
        for (size_t i = 0; i + 1 < size / sizeof(*properties) && properties[i]; i += 2) {
            if (properties[i] != CL_CONTEXT_PLATFORM) return CL_INVALID_PROPERTY;
            uint32_t id = remember(s, (void *)properties[i + 1], PLATFORM, 0);
            if (!id) return CL_OUT_OF_HOST_MEMORY;
            properties[i + 1] = rme_cl_handle(id, PLATFORM);
        }
    }
    if (output && kind == MEMORY && !which && name == CL_MEM_HOST_PTR) memset(output, 0, size);
    if (output && kind == DEVICE && name == CL_DEVICE_EXECUTION_CAPABILITIES && size == sizeof(cl_device_exec_capabilities))
        *(cl_device_exec_capabilities *)output &= ~(cl_device_exec_capabilities)CL_EXEC_NATIVE_KERNEL;
    reply->value = size;
    if (output) reply->bytes = (uint32_t)size;
    return CL_SUCCESS;
}

static cl_int image_type(struct api *api, cl_mem image, cl_mem_object_type *type, size_t *element) {
    cl_int status = api->clGetMemObjectInfo(image, CL_MEM_TYPE, sizeof(*type), type, NULL);
    return status ? status : api->clGetImageInfo(image, CL_IMAGE_ELEMENT_SIZE, sizeof(*element), element, NULL);
}

static cl_int command(struct session *s, const struct rme_cl_request *request, const unsigned char *data,
                      struct rme_cl_reply *reply) {
    const uint64_t *a = request->args;
    struct api *api = &s->api;
    cl_uint count = request->wait_count;
    cl_event wait[RME_CL_LIST_MAX];
    if (count > RME_CL_LIST_MAX || request->bytes < count * sizeof(uint32_t)) return CL_INVALID_EVENT_WAIT_LIST;
    for (cl_uint i = 0; i < count; ++i) {
        uint32_t id;
        memcpy(&id, data + i * sizeof(id), sizeof(id));
        wait[i] = object(s, id, EVENT);
        if (!wait[i]) return CL_INVALID_EVENT;
    }
    size_t bytes = request->bytes - count * sizeof(uint32_t);
    if (count) data += count * sizeof(uint32_t);
    const cl_event *dependencies = count ? wait : NULL;
    cl_event event = NULL;
    cl_event *event_out = request->event_requested ? &event : NULL;
    cl_int status = CL_SUCCESS;
    void *native;
#define GET(kind, index) object(s, a[index], kind)
#define REQUIRE(type, name, kind, index, error) type name = GET(kind, index); if (!name) return error
#define CREATE(kind, expr) do { native = (expr); return status ? status : created(s, reply, native, kind); } while (0)
    switch (request->command) {
    case PLATFORMS:
        reply->object = s->capabilities;
        return enumerate(s, reply, PLATFORM, NULL, 0, a[0]);
    case UNLOAD_COMPILER: {
        REQUIRE(cl_platform_id, platform, PLATFORM, 0, CL_INVALID_PLATFORM);
        return api->clUnloadPlatformCompiler(platform);
    }
    case DEVICES: {
        REQUIRE(cl_platform_id, platform, PLATFORM, 0, CL_INVALID_PLATFORM);
        return enumerate(s, reply, DEVICE, platform, a[1], a[2]);
    }
    case INFO: return query(s, a, reply);
    case CREATE_CONTEXT: {
        REQUIRE(cl_device_id, device, DEVICE, 0, CL_INVALID_DEVICE);
        cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)GET(PLATFORM, 1), 0};
        if (a[1] && !props[1]) return CL_INVALID_PLATFORM;
        CREATE(CONTEXT, api->clCreateContext(a[1] ? props : NULL, 1, &device, NULL, NULL, &status));
    }
    case CREATE_QUEUE: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        REQUIRE(cl_device_id, device, DEVICE, 1, CL_INVALID_DEVICE);
        CREATE(QUEUE, api->clCreateCommandQueue(context, device, a[2], &status));
    }
    case CREATE_BUFFER: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        if (bytes && bytes != a[2]) return CL_INVALID_HOST_PTR;
        cl_mem_flags flags = a[1];
        if (flags & CL_MEM_USE_HOST_PTR) flags = (flags & ~CL_MEM_USE_HOST_PTR) | CL_MEM_COPY_HOST_PTR;
        CREATE(MEMORY, api->clCreateBuffer(context, flags, a[2], bytes ? (void *)data : NULL, &status));
    }
    case CREATE_SUB_BUFFER: {
        REQUIRE(cl_mem, buffer, MEMORY, 0, CL_INVALID_MEM_OBJECT);
        cl_buffer_region region = {a[2], a[3]};
        CREATE(MEMORY, api->clCreateSubBuffer(buffer, a[1], CL_BUFFER_CREATE_TYPE_REGION, &region, &status));
    }
    case CREATE_IMAGE: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        cl_image_format format;
        if (bytes < sizeof(format)) return CL_INVALID_IMAGE_FORMAT_DESCRIPTOR;
        memcpy(&format, data, sizeof(format));
        cl_image_desc desc = {.image_type = a[2], .image_width = a[3], .image_height = a[4], .image_depth = a[5],
            .image_array_size = a[6], .image_row_pitch = a[7], .image_slice_pitch = a[8], .buffer = GET(MEMORY, 9)};
        if (a[9] && !desc.buffer) return CL_INVALID_MEM_OBJECT;
        cl_mem_flags flags = a[1];
        void *pixels = NULL;
        if (bytes > sizeof(format)) {
            size_t region[3];
            struct image_layout layout;
            descriptor_region(&desc, region);
            if (!layout_image(&layout, desc.image_type, region, pixel_size(format), desc.image_row_pitch,
                              desc.image_slice_pitch) || bytes - sizeof(format) != layout.bytes) return CL_INVALID_IMAGE_SIZE;
            pixels = (void *)(data + sizeof(format));
        }
        if (flags & CL_MEM_USE_HOST_PTR) flags = (flags & ~CL_MEM_USE_HOST_PTR) | CL_MEM_COPY_HOST_PTR;
        CREATE(MEMORY, api->clCreateImage(context, flags, &format, &desc, pixels, &status));
    }
    case IMAGE_FORMATS: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        cl_uint limit = a[3];
        if (limit > RME_CL_LIST_MAX) limit = RME_CL_LIST_MAX;
        cl_image_format *formats = reserve(&s->output, limit * sizeof(*formats));
        if (limit && !formats) return CL_OUT_OF_HOST_MEMORY;
        cl_uint found = 0;
        status = api->clGetSupportedImageFormats(context, a[1], a[2], limit, formats, &found);
        if (!status) {
            reply->value = found;
            reply->bytes = (found < limit ? found : limit) * sizeof(*formats);
        }
        return status;
    }
    case IMAGE_LAYOUT: {
        REQUIRE(cl_mem, image, MEMORY, 0, CL_INVALID_MEM_OBJECT);
        cl_mem_object_type type;
        size_t element;
        status = image_type(api, image, &type, &element);
        if (!status) { reply->object = type; reply->value = element; }
        return status;
    }
    case READ_IMAGE: case WRITE_IMAGE: case COPY_IMAGE: case IMAGE_TO_BUFFER: case BUFFER_TO_IMAGE: case FILL_IMAGE: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_mem, image, MEMORY, 1, CL_INVALID_MEM_OBJECT);
        size_t origin[] = {a[2], a[3], a[4]};
        size_t region[] = {a[5], a[6], a[7]};
        if (request->command == FILL_IMAGE) {
            if (bytes != 4 * sizeof(cl_uint)) return CL_INVALID_VALUE;
            cl_uint color[4];
            memcpy(color, data, sizeof(color));
            status = api->clEnqueueFillImage(queue, image, color, origin, region, count, dependencies, event_out);
            break;
        }
        if (request->command == IMAGE_TO_BUFFER || request->command == BUFFER_TO_IMAGE) {
            REQUIRE(cl_mem, buffer, MEMORY, 8, CL_INVALID_MEM_OBJECT);
            status = request->command == IMAGE_TO_BUFFER
                ? api->clEnqueueCopyImageToBuffer(queue, image, buffer, origin, region, a[9], count, dependencies, event_out)
                : api->clEnqueueCopyBufferToImage(queue, buffer, image, a[9], origin, region, count, dependencies, event_out);
            break;
        }
        if (request->command == COPY_IMAGE) {
            REQUIRE(cl_mem, target, MEMORY, 8, CL_INVALID_MEM_OBJECT);
            size_t target_origin[3];
            if (bytes != sizeof(target_origin)) return CL_INVALID_VALUE;
            memcpy(target_origin, data, bytes);
            status = api->clEnqueueCopyImage(queue, image, target, origin, target_origin, region, count, dependencies, event_out);
            break;
        }
        cl_mem_object_type type;
        size_t element;
        status = image_type(api, image, &type, &element);
        if (status) return status;
        struct image_layout layout;
        if (!layout_image(&layout, type, region, element, 0, 0)) return CL_INVALID_VALUE;
        if (request->command == READ_IMAGE) {
            void *out = reserve(&s->output, layout.bytes);
            if (!out) return CL_OUT_OF_HOST_MEMORY;
            status = api->clEnqueueReadImage(queue, image, CL_TRUE, origin, region, 0, 0, out, count, dependencies, event_out);
            if (!status) reply->bytes = (uint32_t)layout.bytes;
        } else {
            if (bytes != layout.bytes) return CL_INVALID_VALUE;
            status = write_image(s, queue, image, origin, region, bytes, data, count, dependencies,
                                 event_out, a[10] != 0);
        }
        break;
    }
    case SOURCE: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        if (!bytes) return CL_INVALID_VALUE;
        const char *source = (const char *)data;
        CREATE(PROGRAM, api->clCreateProgramWithSource(context, 1, &source, &bytes, &status));
    }
    case BINARY: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        REQUIRE(cl_device_id, device, DEVICE, 1, CL_INVALID_DEVICE);
        if (!bytes) return CL_INVALID_VALUE;
        cl_int binary_status = CL_SUCCESS;
        native = api->clCreateProgramWithBinary(context, 1, &device, &bytes, &data, &binary_status, &status);
        reply->value = (uint32_t)binary_status;
        return status ? status : created(s, reply, native, PROGRAM);
    }
    case BINARIES: {
        REQUIRE(cl_program, program, PROGRAM, 0, CL_INVALID_PROGRAM);
        cl_uint device_count;
        status = api->clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(device_count), &device_count, NULL);
        if (status) return status;
        if (device_count != 1) return CL_INVALID_OPERATION;
        size_t n;
        status = api->clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(n), &n, NULL);
        if (status || !n) return status;
        unsigned char *binary = reserve(&s->output, n);
        if (!binary) return CL_OUT_OF_HOST_MEMORY;
        status = api->clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(binary), &binary, NULL);
        if (!status) reply->bytes = (uint32_t)n;
        return status;
    }
    case BUILTIN_PROGRAM: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        REQUIRE(cl_device_id, device, DEVICE, 1, CL_INVALID_DEVICE);
        if (!terminated(data, bytes)) return CL_INVALID_VALUE;
        CREATE(PROGRAM, api->clCreateProgramWithBuiltInKernels(context, 1, &device, (const char *)data, &status));
    }
    case BUILD_PROGRAM: case COMPILE_PROGRAM: case LINK_PROGRAM: {
        cl_device_id device = GET(DEVICE, 1);
        if (a[1] && !device) return CL_INVALID_DEVICE;
        size_t option_bytes = a[2];
        if (option_bytes > bytes || (option_bytes && !terminated(data, option_bytes))) return CL_INVALID_BUILD_OPTIONS;
        const char *options = option_bytes ? (const char *)data : NULL;
        if (option_bytes) data += option_bytes;
        bytes -= option_bytes;
        if (request->command == BUILD_PROGRAM) {
            REQUIRE(cl_program, program, PROGRAM, 0, CL_INVALID_PROGRAM);
            return api->clBuildProgram(program, device ? 1 : 0, device ? &device : NULL, options, NULL, NULL);
        }
        cl_program inputs[32];
        const char *names[32];
        cl_uint n = a[3];
        if (a[3] > 32 || bytes < n * sizeof(uint32_t)) return CL_INVALID_VALUE;
        for (cl_uint i = 0; i < n; ++i) {
            uint32_t id;
            memcpy(&id, data + i * sizeof(id), sizeof(id));
            inputs[i] = object(s, id, PROGRAM);
            if (!inputs[i]) return CL_INVALID_PROGRAM;
        }
        if (n) data += n * sizeof(uint32_t);
        bytes -= n * sizeof(uint32_t);
        if (request->command == LINK_PROGRAM) {
            REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
            CREATE(PROGRAM, api->clLinkProgram(context, device ? 1 : 0, device ? &device : NULL, options,
                                              n, n ? inputs : NULL, NULL, NULL, &status));
        }
        REQUIRE(cl_program, program, PROGRAM, 0, CL_INVALID_PROGRAM);
        for (cl_uint i = 0; i < n; ++i) {
            size_t length = 0;
            while (length < bytes && data[length]) ++length;
            if (length == bytes) return CL_INVALID_VALUE;
            names[i] = (const char *)data;
            data += length + 1;
            bytes -= length + 1;
        }
        return api->clCompileProgram(program, device ? 1 : 0, device ? &device : NULL, options, n,
                                     n ? inputs : NULL, n ? names : NULL, NULL, NULL);
    }
    case CREATE_KERNEL: {
        REQUIRE(cl_program, program, PROGRAM, 0, CL_INVALID_PROGRAM);
        if (!terminated(data, bytes)) return CL_INVALID_KERNEL_NAME;
        CREATE(KERNEL, api->clCreateKernel(program, (const char *)data, &status));
    }
    case KERNELS: {
        REQUIRE(cl_program, program, PROGRAM, 0, CL_INVALID_PROGRAM);
        cl_kernel kernels[64];
        cl_uint amount = 0, n = a[1] > 64 ? 64 : (cl_uint)a[1];
        uint32_t *ids = reserve(&s->output, n * sizeof(*ids));
        if (n && !ids) return CL_OUT_OF_HOST_MEMORY;
        status = api->clCreateKernelsInProgram(program, n, n ? kernels : NULL, &amount);
        if (status) return status;
        reply->value = amount;
        if (!n) return CL_SUCCESS;
        for (cl_uint i = 0; i < amount; ++i) {
            ids[i] = remember(s, kernels[i], KERNEL, 1);
            if (!ids[i]) {
                for (cl_uint j = i; j < amount; ++j) api->clReleaseKernel(kernels[j]);
                for (cl_uint j = 0; j < i; ++j) {
                    api->clReleaseKernel(kernels[j]);
                    s->objects[ids[j] - 1] = (struct object){0};
                }
                return CL_OUT_OF_HOST_MEMORY;
            }
        }
        reply->bytes = amount * sizeof(*ids);
        return CL_SUCCESS;
    }
    case SET_ARG: {
        REQUIRE(cl_kernel, kernel, KERNEL, 0, CL_INVALID_KERNEL);
        if (a[3]) {
            if (a[3] != MEMORY && a[3] != SAMPLER) return CL_INVALID_ARG_VALUE;
            native = object(s, a[4], a[3]);
            if (!native || a[2] != sizeof(native)) return CL_INVALID_ARG_VALUE;
            return api->clSetKernelArg(kernel, a[1], sizeof(native), &native);
        }
        if (bytes && bytes != a[2]) return CL_INVALID_ARG_SIZE;
        return api->clSetKernelArg(kernel, a[1], a[2], bytes ? data : NULL);
    }
    case DISPATCH: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_kernel, kernel, KERNEL, 1, CL_INVALID_KERNEL);
        size_t ranges[3][3];
        if (!a[2] || a[2] > 3 || bytes != sizeof(ranges)) return CL_INVALID_WORK_DIMENSION;
        memcpy(ranges, data, sizeof(ranges));
        status = api->clEnqueueNDRangeKernel(queue, kernel, a[2], a[3] ? ranges[0] : NULL,
                                            ranges[1], a[4] ? ranges[2] : NULL, count, dependencies, event_out);
        break;
    }
    case TASK: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_kernel, kernel, KERNEL, 1, CL_INVALID_KERNEL);
        status = api->clEnqueueTask(queue, kernel, count, dependencies, event_out);
        break;
    }
    case MIGRATE: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        cl_mem memories[64];
        if (!bytes || bytes % sizeof(uint32_t) || bytes > 64 * sizeof(uint32_t)) return CL_INVALID_VALUE;
        cl_uint n = bytes / sizeof(uint32_t);
        for (cl_uint i = 0; i < n; ++i) {
            uint32_t id;
            memcpy(&id, data + i * sizeof(id), sizeof(id));
            memories[i] = object(s, id, MEMORY);
            if (!memories[i]) return CL_INVALID_MEM_OBJECT;
        }
        status = api->clEnqueueMigrateMemObjects(queue, n, memories, a[1], count, dependencies, event_out);
        break;
    }
    case READ_BUFFER_RECT: case WRITE_BUFFER_RECT: case COPY_BUFFER_RECT: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_mem, buffer, MEMORY, 1, CL_INVALID_MEM_OBJECT);
        size_t origin[] = {a[2], a[3], a[4]}, region[] = {a[5], a[6], a[7]}, zero[3] = {0};
        if (request->command == COPY_BUFFER_RECT) {
            REQUIRE(cl_mem, target, MEMORY, 10, CL_INVALID_MEM_OBJECT);
            size_t rect[5];
            if (bytes != sizeof(rect)) return CL_INVALID_VALUE;
            memcpy(rect, data, bytes);
            status = api->clEnqueueCopyBufferRect(queue, buffer, target, origin, rect, region, a[8], a[9],
                                                  rect[3], rect[4], count, dependencies, event_out);
        } else {
            struct image_layout layout;
            if (!layout_image(&layout, CL_MEM_OBJECT_IMAGE3D, region, 1, 0, 0)) return CL_INVALID_VALUE;
            if (request->command == READ_BUFFER_RECT) {
                void *out = reserve(&s->output, layout.bytes);
                if (!out) return CL_OUT_OF_HOST_MEMORY;
                status = api->clEnqueueReadBufferRect(queue, buffer, CL_TRUE, origin, zero, region, a[8], a[9], 0, 0,
                                                      out, count, dependencies, event_out);
                if (!status) reply->bytes = (uint32_t)layout.bytes;
            } else {
                if (bytes != layout.bytes) return CL_INVALID_VALUE;
                int nonblocking = a[10] != 0;
                struct upload *upload = nonblocking ? stage_upload(s, data, bytes) : NULL;
                if (nonblocking && !upload) return CL_OUT_OF_HOST_MEMORY;
                cl_event upload_event = NULL;
                status = api->clEnqueueWriteBufferRect(queue, buffer, nonblocking ? CL_FALSE : CL_TRUE,
                                                       origin, zero, region, a[8], a[9], 0, 0,
                                                       upload ? upload->data : data, count, dependencies,
                                                       upload ? &upload_event : event_out);
                if (upload) status = track_upload(api, queue, upload, status, upload_event, event_out);
            }
        }
        break;
    }
    case READ_BUFFER_ASYNC: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_mem, buffer, MEMORY, 1, CL_INVALID_MEM_OBJECT);
        return submit_read(s, request, reply, queue, buffer, count, dependencies);
    }
    case READ_BUFFER: case WRITE_BUFFER: case COPY_BUFFER: case FILL_BUFFER: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        REQUIRE(cl_mem, buffer, MEMORY, 1, CL_INVALID_MEM_OBJECT);
        if (request->command == READ_BUFFER) {
            void *out = reserve(&s->output, a[3]);
            if (!out) return CL_INVALID_VALUE;
            status = api->clEnqueueReadBuffer(queue, buffer, CL_TRUE, a[2], a[3], out, count, dependencies, event_out);
            if (!status) reply->bytes = (uint32_t)a[3];
        } else if (request->command == WRITE_BUFFER) {
            if (!bytes) return CL_INVALID_VALUE;
            status = write_buffer(s, queue, buffer, a[2], bytes, data, count, dependencies, event_out, a[10] != 0);
        } else if (request->command == FILL_BUFFER) {
            if (!bytes) return CL_INVALID_VALUE;
            status = api->clEnqueueFillBuffer(queue, buffer, data, bytes, a[2], a[3], count, dependencies, event_out);
        } else {
            REQUIRE(cl_mem, target, MEMORY, 4, CL_INVALID_MEM_OBJECT);
            status = api->clEnqueueCopyBuffer(queue, buffer, target, a[2], a[5], a[3], count, dependencies, event_out);
        }
        break;
    }
    case RETAIN: case RELEASE:
        return change_reference(s, a[1], a[0], request->command == RETAIN);
    case FLUSH: case FINISH: case MARKER: case BARRIER: {
        REQUIRE(cl_command_queue, queue, QUEUE, 0, CL_INVALID_COMMAND_QUEUE);
        if (request->command == FLUSH) return api->clFlush(queue);
        if (request->command == FINISH) return api->clFinish(queue);
        status = request->command == MARKER ? api->clEnqueueMarkerWithWaitList(queue, count, dependencies, event_out)
                                           : api->clEnqueueBarrierWithWaitList(queue, count, dependencies, event_out);
        break;
    }
    case WAIT_EVENTS: return api->clWaitForEvents(count, dependencies);
    case CREATE_SAMPLER: {
        REQUIRE(cl_context, context, CONTEXT, 0, CL_INVALID_CONTEXT);
        CREATE(SAMPLER, api->clCreateSampler(context, a[1], a[2], a[3], &status));
    }
    default: return CL_INVALID_OPERATION;
    }
#undef CREATE
#undef REQUIRE
#undef GET
    if (!status && event) status = created(s, reply, event, EVENT);
    return status;
}

int rme_cl_abi_serve(void *stream, rme_cl_abi_read read, rme_cl_abi_write write, rme_cl_abi_share share,
                     rme_cl_abi_share_readback share_readback, void *driver, rme_cl_abi_resolve resolve) {
    struct session s = {.capabilities = RME_CL_CAP_INLINE_RELEASE | RME_CL_CAP_ASYNC_READ};
    if (share_readback) s.capabilities |= RME_CL_CAP_READBACK_SLOTS;
    if (load_api(&s.api, driver, resolve)) return -1;
    int result;
    struct rme_cl_request request;
    struct bytes mapping = {0};
#ifdef RME_CL_PROFILE
    struct call_metrics metrics[COPY_BUFFER_RECT + 1] = {0};
    const uint64_t session_start = monotonic_ns();
#endif
    while ((result = read(stream, &request, sizeof(request))) == 1) {
        if (request.magic != RME_CL_ABI_MAGIC || request.bytes > RME_CL_ABI_MAX) { result = -1; break; }
        struct rme_cl_reply reply = {.magic = RME_CL_ABI_MAGIC};
        if (request.command == SHARE_BUFFER) {
            if (request.bytes) { result = -1; break; }
            size_t size = request.args[0];
            void *data = share && size >= RME_CL_SHARED_MIN && size <= RME_CL_ABI_MAX ? share(stream, size) : NULL;
            if (data) mapping = (struct bytes){data, size, 1};
            else {
                reply.status = CL_INVALID_OPERATION;
                if (write(stream, &reply, NULL, 0)) { result = -1; break; }
            }
            continue;
        }
        if (request.command == SHARE_READBACK) {
            if (request.bytes) { result = -1; break; }
            if (!s.readback && share_readback && (s.readback = share_readback(stream))) continue;
            reply.status = CL_INVALID_OPERATION;
            if (write(stream, &reply, NULL, 0)) { result = -1; break; }
            continue;
        }
        const int shared = (request.command & RME_CL_SHARED_DATA) != 0;
        const int async_release = (request.command & RME_CL_ASYNC_RELEASE) != 0;
        const int read_shared = (request.command & RME_CL_READ_SHARED_READY) != 0;
        request.command &= ~(RME_CL_SHARED_DATA | RME_CL_ASYNC_RELEASE | RME_CL_READ_SHARED_READY);
        if (shared && (!mapping.data || request.bytes > mapping.capacity)) { result = -1; break; }
        if (async_release && (shared || request.command != RELEASE || request.args[0] != EVENT || request.bytes)) {
            result = -1;
            break;
        }
        if (request.release_event && change_reference(&s, request.release_event, EVENT, 0) != CL_SUCCESS) {
            result = -1;
            break;
        }
#ifdef RME_CL_PROFILE
        uint64_t started = monotonic_ns();
#endif
        unsigned char *data = shared ? mapping.data : reserve(&s.input, request.bytes);
        if (!shared && request.bytes && (!data || read(stream, data, request.bytes) != 1)) { result = -1; break; }
        struct bytes stream_output = s.output;
        if (shared) s.output = mapping;
#ifdef RME_CL_PROFILE
        uint64_t entered = monotonic_ns();
#endif
        reply.status = command(&s, &request, data, &reply);
        if (reply.status) reply.bytes = 0;
        if (shared) s.output = stream_output;
#ifdef RME_CL_PROFILE
        uint64_t left = monotonic_ns();
#endif
        const struct bytes *completion_mapping = (shared || read_shared) && !(shared && reply.bytes) ? &mapping : NULL;
        if (async_release ? reply.status != CL_SUCCESS :
            (s.reads && complete_reads(&s, stream, write, completion_mapping)) ||
            write(stream, &reply, s.output.data, shared ? 0 : reply.bytes)) {
            result = -1;
            break;
        }
#ifdef RME_CL_PROFILE
        unsigned op = request.command == READ_BUFFER_ASYNC ? READ_BUFFER : request.command;
        if (op <= COPY_BUFFER_RECT) {
            struct call_metrics *entry = metrics + op;
            ++entry->calls;
            entry->input_bytes += request.bytes;
            entry->output_bytes += reply.bytes;
            entry->input_ns += entered - started;
            entry->driver_ns += left - entered;
            entry->output_ns += monotonic_ns() - left;
            if (shared) entry->shared_bytes += (uint64_t)request.bytes + reply.bytes;
            if ((op == READ_BUFFER || op == WRITE_BUFFER) && request.args[10])
                ++entry->nonblocking;
        }
#endif
    }
#ifdef RME_CL_PROFILE
    for (unsigned op = 1; op <= COPY_BUFFER_RECT; ++op) {
        const struct call_metrics *entry = metrics + op;
        if (!entry->calls) continue;
        __android_log_print(ANDROID_LOG_INFO, "RmeOpenCLProfile",
            "session=%llu op=%u calls=%llu input_bytes=%llu output_bytes=%llu input_ns=%llu driver_ns=%llu output_ns=%llu nonblocking=%llu shared_bytes=%llu",
            (unsigned long long)session_start, op, (unsigned long long)entry->calls,
            (unsigned long long)entry->input_bytes, (unsigned long long)entry->output_bytes,
            (unsigned long long)entry->input_ns, (unsigned long long)entry->driver_ns,
            (unsigned long long)entry->output_ns, (unsigned long long)entry->nonblocking, (unsigned long long)entry->shared_bytes);
    }
#endif
    while (s.reads) {
        struct pending_read *read = s.reads;
        s.reads = read->next;
        retire_read(&s.api, read);
    }
    if (s.readback) release_readback(s.readback);
    for (uint32_t i = s.count; i; --i) {
        struct object *entry = s.objects + i - 1;
        while (entry->refs) {
            --entry->refs;
            reference(&s.api, entry->native, entry->kind, 0);
        }
    }
    free(s.objects);
    free(s.input.data);
    free(s.output.data);
    if (s.uploads) release_upload_budget(s.uploads);
    return result;
}
