#include "openclbridge.h"

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#define OPENCL_FUNCTIONS(X) \
    X(clGetPlatformIDs) X(clGetDeviceIDs) X(clGetDeviceInfo) \
    X(clCreateContext) X(clReleaseContext) \
    X(clCreateCommandQueue) X(clReleaseCommandQueue) \
    X(clCreateProgramWithSource) X(clBuildProgram) X(clGetProgramBuildInfo) X(clReleaseProgram) \
    X(clCreateKernel) X(clSetKernelArg) X(clReleaseKernel) \
    X(clCreateBuffer) X(clEnqueueWriteBuffer) X(clEnqueueReadBuffer) X(clReleaseMemObject) \
    X(clEnqueueNDRangeKernel) X(clWaitForEvents) X(clGetEventProfilingInfo) X(clReleaseEvent)

#define FIELD(name) __typeof__(&name) name;
struct api { OPENCL_FUNCTIONS(FIELD) };
#undef FIELD

enum { MAX_PLATFORMS = 32, MAX_DEVICES = 32, BUFFER_SLOTS = 8 };

struct session {
    struct api api;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem buffers[BUFFER_SLOTS];
};

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}

size_t rme_cl_session_size(void) { return sizeof(struct session); }

int rme_cl_init(void *session, rme_cl_resolve resolve, void *library) {
    struct session *s = session;
    s->device = NULL;
    s->context = NULL;
    s->queue = NULL;
    s->program = NULL;
    s->kernel = NULL;
    for (unsigned i = 0; i < BUFFER_SLOTS; ++i) s->buffers[i] = NULL;
#define LOAD(name) \
    s->api.name = (__typeof__(&name))resolve(library, #name); \
    if (!s->api.name) return CL_INVALID_OPERATION;
    OPENCL_FUNCTIONS(LOAD)
#undef LOAD
    return CL_SUCCESS;
}

void rme_cl_destroy(void *session) {
    struct session *s = session;
    if (s->kernel) s->api.clReleaseKernel(s->kernel);
    if (s->program) s->api.clReleaseProgram(s->program);
    for (unsigned i = 0; i < BUFFER_SLOTS; ++i) {
        if (s->buffers[i]) s->api.clReleaseMemObject(s->buffers[i]);
    }
    if (s->queue) s->api.clReleaseCommandQueue(s->queue);
    if (s->context) s->api.clReleaseContext(s->context);
}

static int open_device(struct session *s, const unsigned char *name, uint32_t size,
                       unsigned char *output, uint32_t capacity, uint32_t *written) {
    if (s->context) return CL_INVALID_OPERATION;
    if (size < 2 || name[size - 1]) return CL_INVALID_VALUE;

    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint count = 0;
    cl_int status = s->api.clGetPlatformIDs(MAX_PLATFORMS, platforms, &count);
    if (status) return status;
    if (count > MAX_PLATFORMS) return CL_OUT_OF_RESOURCES;

    for (cl_uint i = 0; i < count; ++i) {
        cl_device_id devices[MAX_DEVICES];
        cl_uint device_count = 0;
        status = s->api.clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &device_count);
        if (status == CL_DEVICE_NOT_FOUND) continue;
        if (status) return status;
        if (device_count > MAX_DEVICES) return CL_OUT_OF_RESOURCES;
        for (cl_uint j = 0; j < device_count; ++j) {
            size_t name_size, version_size;
            cl_device_id device = devices[j];
            status = s->api.clGetDeviceInfo(device, CL_DEVICE_NAME, 0, NULL, &name_size);
            if (status) return status;
            if (name_size != size) continue;
            status = s->api.clGetDeviceInfo(device, CL_DEVICE_NAME, capacity, output, NULL);
            if (status) return status;
            size_t matched = 0;
            while (matched < name_size && name[matched] == output[matched]) ++matched;
            if (matched != name_size) continue;

            status = s->api.clGetDeviceInfo(device, CL_DRIVER_VERSION, capacity - name_size,
                                            output + name_size, &version_size);
            if (status) return status;
            cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[i], 0};
            cl_context context = s->api.clCreateContext(properties, 1, &device, NULL, NULL, &status);
            if (status) return status;
            cl_command_queue queue = s->api.clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
            if (status) {
                s->api.clReleaseContext(context);
                return status;
            }
            s->device = device;
            s->context = context;
            s->queue = queue;
            *written = (uint32_t)(name_size + version_size);
            return CL_SUCCESS;
        }
    }
    return CL_DEVICE_NOT_FOUND;
}

static int build_kernel(struct session *s, const unsigned char *input, uint32_t size,
                        void *output, uint32_t capacity, uint32_t *written) {
    if (size < 7) return CL_INVALID_VALUE;
    uint32_t name_size = get32(input);
    if (name_size < 2 || name_size >= size - 4 || input[3 + name_size]) return CL_INVALID_VALUE;
    const char *name = (const char *)input + 4;
    const char *source = name + name_size;
    size_t source_size = size - 4 - name_size;
    cl_int status;
    cl_program program = s->api.clCreateProgramWithSource(s->context, 1, &source, &source_size, &status);
    if (status) return status;
    status = s->api.clBuildProgram(program, 1, &s->device, NULL, NULL, NULL);
    if (status) {
        size_t log_size;
        if (!s->api.clGetProgramBuildInfo(program, s->device, CL_PROGRAM_BUILD_LOG, capacity, output, &log_size))
            *written = (uint32_t)log_size;
        s->api.clReleaseProgram(program);
        return status;
    }
    cl_kernel kernel = s->api.clCreateKernel(program, name, &status);
    if (status) {
        s->api.clReleaseProgram(program);
        return status;
    }
    if (s->kernel) s->api.clReleaseKernel(s->kernel);
    if (s->program) s->api.clReleaseProgram(s->program);
    s->program = program;
    s->kernel = kernel;
    return CL_SUCCESS;
}

static int run_kernel(struct session *s, size_t count, cl_mem buffer, uint32_t offset, uint32_t bytes,
                      unsigned char *output) {
    cl_event event;
    cl_int status = s->api.clEnqueueNDRangeKernel(s->queue, s->kernel, 1, NULL, &count, NULL, 0, NULL, &event);
    if (status) return status;
    cl_ulong start, end;
    // A blocking read on the in-order queue also waits for the preceding kernel.
    if (buffer) {
        status = s->api.clEnqueueReadBuffer(s->queue, buffer, CL_TRUE, offset, bytes,
                                            output + 8, 0, NULL, NULL);
    } else {
        status = s->api.clWaitForEvents(1, &event);
    }
    if (!status) status = s->api.clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(start), &start, NULL);
    if (!status) status = s->api.clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(end), &end, NULL);
    s->api.clReleaseEvent(event);
    if (!status) {
        put32(output, (uint32_t)(end - start));
        put32(output + 4, (uint32_t)((end - start) >> 32));
    }
    return status;
}

static int run_request(struct session *s, uint32_t op, const unsigned char *input, uint32_t size,
                       void *output, uint32_t capacity, uint32_t *written) {
    if (!s->kernel) return CL_INVALID_KERNEL;
    if (capacity < 8 || size != (op == RME_CL_RUN ? 4u : 16u)) return CL_INVALID_VALUE;
    cl_mem read_buffer = NULL;
    uint32_t offset = 0, bytes = 0;
    if (op == RME_CL_RUN_READ) {
        uint32_t slot = get32(input + 4);
        if (slot >= BUFFER_SLOTS || !s->buffers[slot]) return CL_INVALID_MEM_OBJECT;
        read_buffer = s->buffers[slot];
        offset = get32(input + 8);
        bytes = get32(input + 12);
        if (!bytes || bytes > capacity - 8) return CL_INVALID_VALUE;
    }
    cl_int status = run_kernel(s, get32(input), read_buffer, offset, bytes, output);
    if (!status) *written = 8 + bytes;
    return status;
}

int rme_cl_call(void *session, uint32_t op, const void *payload, uint32_t size,
                void *output, uint32_t capacity, uint32_t *written) {
    struct session *s = session;
    const unsigned char *input = payload;
    *written = 0;
    if (op == RME_CL_OPEN) return open_device(s, input, size, output, capacity, written);
    if (!s->context) return CL_INVALID_CONTEXT;
    if (op == RME_CL_BUILD) return build_kernel(s, input, size, output, capacity, written);

    if (op == RME_CL_RUN || op == RME_CL_RUN_READ)
        return run_request(s, op, input, size, output, capacity, written);

    if (op == RME_CL_VALUE_ARG || op == RME_CL_MEMORY_ARG) {
        if (!s->kernel) return CL_INVALID_KERNEL;
        if (size < 8) return CL_INVALID_VALUE;
        cl_uint index = get32(input);
        if (op == RME_CL_VALUE_ARG) return s->api.clSetKernelArg(s->kernel, index, size - 4, input + 4);
        if (size != 8) return CL_INVALID_VALUE;
        uint32_t slot = get32(input + 4);
        if (slot >= BUFFER_SLOTS || !s->buffers[slot]) return CL_INVALID_MEM_OBJECT;
        return s->api.clSetKernelArg(s->kernel, index, sizeof(cl_mem), &s->buffers[slot]);
    }

    if (size < 4) return CL_INVALID_VALUE;
    uint32_t slot = get32(input);
    if (slot >= BUFFER_SLOTS) return CL_INVALID_MEM_OBJECT;
    cl_mem buffer = s->buffers[slot];
    if (op == RME_CL_ALLOC) {
        if (size != 8) return CL_INVALID_VALUE;
        if (buffer) return CL_INVALID_OPERATION;
        cl_int status;
        s->buffers[slot] = s->api.clCreateBuffer(s->context, CL_MEM_READ_WRITE, get32(input + 4), NULL, &status);
        return status;
    }

    if (!buffer) return CL_INVALID_MEM_OBJECT;
    switch (op) {
    case RME_CL_WRITE:
        if (size <= 8) return CL_INVALID_VALUE;
        return s->api.clEnqueueWriteBuffer(s->queue, buffer, CL_TRUE, get32(input + 4), size - 8,
                                            input + 8, 0, NULL, NULL);
    case RME_CL_READ: {
        if (size != 12) return CL_INVALID_VALUE;
        uint32_t bytes = get32(input + 8);
        if (bytes > capacity) return CL_INVALID_VALUE;
        cl_int status = s->api.clEnqueueReadBuffer(s->queue, buffer, CL_TRUE, get32(input + 4), bytes,
                                                   output, 0, NULL, NULL);
        if (!status) *written = bytes;
        return status;
    }
    case RME_CL_RELEASE:
        if (size != 4) return CL_INVALID_VALUE;
        s->buffers[slot] = NULL;
        return s->api.clReleaseMemObject(buffer);
    default:
        return CL_INVALID_OPERATION;
    }
}

int rme_cl_serve(void *session, void *stream, rme_cl_read read, rme_cl_write write,
                 void *input, void *output) {
    unsigned char *header = output;
    for (;;) {
        int received = read(stream, header, RME_CL_HEADER_SIZE);
        if (received <= 0) return received;
        uint32_t op = get32(header + 4);
        uint32_t size = get32(header + 8);
        if (get32(header) != RME_CL_MAGIC || size > RME_CL_PAYLOAD_MAX) return -1;
        if (size && read(stream, input, size) != 1) return -1;

        uint32_t written;
        int status = rme_cl_call(session, op, input, size, header + RME_CL_HEADER_SIZE,
                                 RME_CL_PAYLOAD_MAX, &written);
        put32(header + 4, (uint32_t)status);
        put32(header + 8, written);
        if (write(stream, header, RME_CL_HEADER_SIZE + written)) return -1;
    }
}
