/* Minimal OpenCL 1.2 test double. This library never calls a GPU driver. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef intptr_t cl_context_properties;
typedef uint64_t cl_device_type;
typedef uint64_t cl_mem_flags;
typedef uint64_t cl_command_queue_properties;
typedef void *cl_platform_id;
typedef void *cl_device_id;
typedef void *cl_context;
typedef void *cl_command_queue;
typedef void *cl_mem;
typedef void *cl_program;
typedef void *cl_kernel;
typedef void *cl_event;

#define CL_SUCCESS 0
#define CL_DEVICE_NOT_FOUND (-1)
#define CL_BUILD_PROGRAM_FAILURE (-11)
#define CL_INVALID_VALUE (-30)
#define CL_INVALID_OPERATION (-59)
#define CL_DEVICE_TYPE_GPU (1ULL << 2)
#define CL_DEVICE_TYPE 0x1000
#define CL_DEVICE_AVAILABLE 0x1027
#define CL_DEVICE_COMPILER_AVAILABLE 0x1028
#define CL_DEVICE_NAME 0x102b
#define CL_DEVICE_VERSION 0x102f
#define CL_CONTEXT_PLATFORM 0x1084
#define CL_INVALID_PLATFORM (-32)

static int platform_obj, second_platform_obj, device_obj, second_device_obj, context_obj, queue_obj, buffer_obj, program_obj, kernel_obj;
uintptr_t fake_platform_pointer(void) { return (uintptr_t)&platform_obj; }
uintptr_t fake_device_pointer(void) { return (uintptr_t)&device_obj; }
static uint32_t results[64];
static int built, arg_set, executed;

static int scenario(const char *value) {
    const char *current = getenv("RME_FAKE_CL_SCENARIO");
    return current && strcmp(current, value) == 0;
}

static cl_int write_info(size_t src_size, const void *src, size_t size, void *out, size_t *written) {
    if (written) *written = src_size;
    if (out) {
        if (size < src_size) return CL_INVALID_VALUE;
        memcpy(out, src, src_size);
    }
    return CL_SUCCESS;
}

cl_int clGetPlatformIDs(cl_uint capacity, cl_platform_id *out, cl_uint *count) {
    if (scenario("no_platforms")) {
        if (count) *count = 0;
        return -1001;
    }
    cl_uint n = (scenario("multi_platform") || scenario("cpu_platform_then_gpu")) ? 2 : 1;
    if (out && capacity < n) return CL_INVALID_VALUE;
    if (out) {
        out[0] = &platform_obj;
        if (n == 2) out[1] = &second_platform_obj;
    }
    if (count) *count = n;
    return CL_SUCCESS;
}

cl_int clGetPlatformInfo(cl_platform_id platform, cl_uint field, size_t size, void *out, size_t *written) {
    const char name[] = "Fake OpenCL Platform";
    const char vendor[] = "Fake Vendor";
    const char version[] = "OpenCL 1.2 Fake";
    if (platform != &platform_obj && platform != &second_platform_obj) return CL_INVALID_PLATFORM;
    if (out && field == 0x0902 && (scenario("platform_length_short") || scenario("platform_length_long"))) {
        cl_int ret = write_info(sizeof(name), name, size, out, NULL);
        if (written) *written = sizeof(name) + (scenario("platform_length_long") ? 1 : -1);
        return ret;
    }
    switch (field) {
        case 0x0901: return write_info(sizeof(version), version, size, out, written);
        case 0x0902: return write_info(sizeof(name), name, size, out, written);
        case 0x0903: return write_info(sizeof(vendor), vendor, size, out, written);
        default: return CL_INVALID_VALUE;
    }
}

cl_int clGetDeviceIDs(cl_platform_id platform, cl_device_type type, cl_uint capacity,
                      cl_device_id *out, cl_uint *count) {
    if ((platform != &platform_obj && platform != &second_platform_obj) ||
        !(type & CL_DEVICE_TYPE_GPU)) return CL_INVALID_VALUE;
    if (scenario("no_gpu") || (scenario("multi_platform") && platform == &platform_obj)) {
        if (count) *count = 0;
        return CL_DEVICE_NOT_FOUND;
    }
    cl_uint n = scenario("cpu_then_gpu") ? 2 : 1;
    if (out && capacity < n) return CL_INVALID_VALUE;
    if (out) {
        out[0] = scenario("cpu_platform_then_gpu") && platform == &second_platform_obj ?
                 &second_device_obj : &device_obj;
        if (n == 2) out[1] = &second_device_obj;
    }
    if (count) *count = n;
    return CL_SUCCESS;
}

#ifndef MISSING_DEVICE_INFO
cl_int clGetDeviceInfo(cl_device_id device, cl_uint field, size_t size, void *out, size_t *written) {
    cl_uint yes = 1;
    int cpu = scenario("cpu_only") ||
              ((scenario("cpu_then_gpu") || scenario("cpu_platform_then_gpu")) && device == &device_obj);
    cl_device_type type = cpu ? 2 : CL_DEVICE_TYPE_GPU;
    const char *name = cpu ? "Fake CPU" : "Fake GPU";
    char long_name[301];
    if (scenario("long_device")) {
        memset(long_name, 'L', sizeof(long_name) - 1);
        long_name[sizeof(long_name) - 1] = '\0';
        name = long_name;
    }
    const char vendor[] = "Fake Vendor";
    const char version[] = "OpenCL 1.2 fake";
    if (device != &device_obj && device != &second_device_obj) return CL_INVALID_VALUE;
    if (out && field == CL_DEVICE_NAME && (scenario("device_length_short") || scenario("device_length_long"))) {
        size_t n = strlen(name) + 1;
        cl_int ret = write_info(n, name, size, out, NULL);
        if (written) *written = n + (scenario("device_length_long") ? 1 : -1);
        return ret;
    }
    if (out && field == CL_DEVICE_TYPE && (scenario("type_length_short") || scenario("type_length_long"))) {
        cl_int ret = write_info(sizeof(type), &type, size, out, NULL);
        if (written) *written = sizeof(type) + (scenario("type_length_long") ? 1 : -1);
        return ret;
    }
    if (out && field == CL_DEVICE_COMPILER_AVAILABLE &&
        (scenario("compiler_length_short") || scenario("compiler_length_long"))) {
        cl_int ret = write_info(sizeof(yes), &yes, size, out, NULL);
        if (written) *written = sizeof(yes) + (scenario("compiler_length_long") ? 1 : -1);
        return ret;
    }
    switch (field) {
        case CL_DEVICE_AVAILABLE: return write_info(sizeof(yes), &yes, size, out, written);
        case CL_DEVICE_COMPILER_AVAILABLE:
            if (scenario("no_compiler")) yes = 0;
            return write_info(sizeof(yes), &yes, size, out, written);
        case CL_DEVICE_TYPE: return write_info(sizeof(type), &type, size, out, written);
        case CL_DEVICE_NAME: return write_info(strlen(name) + 1, name, size, out, written);
        case 0x102c: return write_info(sizeof(vendor), vendor, size, out, written);
        case CL_DEVICE_VERSION: return write_info(sizeof(version), version, size, out, written);
        default: return CL_INVALID_VALUE;
    }
}
#endif

cl_context clCreateContext(const cl_context_properties *properties, cl_uint count,
                           const cl_device_id *devices,
                           void (*callback)(const char *, const void *, size_t, void *),
                           void *user, cl_int *error) {
    (void)callback; (void)user;
    if (scenario("multi_platform") &&
        (!properties || properties[0] != CL_CONTEXT_PLATFORM ||
         properties[1] != (cl_context_properties)&second_platform_obj || properties[2] != 0)) {
        if (error) *error = CL_INVALID_PLATFORM;
        return NULL;
    }
    if (count != 1 || !devices || devices[0] != &device_obj) {
        if (error) *error = CL_INVALID_VALUE;
        return NULL;
    }
    if (error) *error = CL_SUCCESS;
    return &context_obj;
}

cl_command_queue clCreateCommandQueue(cl_context context, cl_device_id device,
                                      cl_command_queue_properties properties, cl_int *error) {
    (void)properties;
    if (context != &context_obj || device != &device_obj) {
        if (error) *error = CL_INVALID_VALUE;
        return NULL;
    }
    if (error) *error = CL_SUCCESS;
    return &queue_obj;
}

cl_mem clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size, void *host, cl_int *error) {
    (void)flags; (void)host;
    if (context != &context_obj || size < sizeof(results)) {
        if (error) *error = CL_INVALID_VALUE;
        return NULL;
    }
    if (error) *error = CL_SUCCESS;
    return &buffer_obj;
}

cl_program clCreateProgramWithSource(cl_context context, cl_uint count, const char **sources,
                                    const size_t *lengths, cl_int *error) {
    (void)lengths;
    if (context != &context_obj || count != 1 || !sources || !sources[0] ||
        !strstr(sources[0], "get_global_id")) {
        if (error) *error = CL_INVALID_VALUE;
        return NULL;
    }
    if (error) *error = CL_SUCCESS;
    return &program_obj;
}

#ifndef MISSING_BUILD
cl_int clBuildProgram(cl_program program, cl_uint count, const cl_device_id *devices,
                      const char *options, void (*callback)(cl_program, void *), void *user) {
    (void)options; (void)callback; (void)user;
    if (program != &program_obj || count != 1 || !devices || devices[0] != &device_obj)
        return CL_INVALID_VALUE;
    if (scenario("build_fail")) return CL_BUILD_PROGRAM_FAILURE;
    built = 1;
    return CL_SUCCESS;
}
#endif

cl_kernel clCreateKernel(cl_program program, const char *name, cl_int *error) {
    if (program != &program_obj || !built || !name || !*name) {
        if (error) *error = CL_INVALID_VALUE;
        return NULL;
    }
    if (error) *error = CL_SUCCESS;
    return &kernel_obj;
}

cl_int clSetKernelArg(cl_kernel kernel, cl_uint index, size_t size, const void *value) {
    if (kernel != &kernel_obj || index != 0 || size != sizeof(cl_mem) || !value ||
        *(const cl_mem *)value != &buffer_obj) return CL_INVALID_VALUE;
    arg_set = 1;
    return CL_SUCCESS;
}

cl_int clEnqueueNDRangeKernel(cl_command_queue queue, cl_kernel kernel, cl_uint dimensions,
                              const size_t *offset, const size_t *global, const size_t *local,
                              cl_uint wait_count, const cl_event *wait, cl_event *event) {
    (void)local; (void)wait_count; (void)wait; (void)event;
    if (queue != &queue_obj || kernel != &kernel_obj || !arg_set || dimensions != 1 ||
        !global || global[0] != 64 || (offset && offset[0] != 0)) return CL_INVALID_VALUE;
    for (uint32_t i = 0; i < 64; ++i) results[i] = i * 3u + 7u;
    if (scenario("bad_result")) results[37]++;
    executed = 1;
    return CL_SUCCESS;
}

cl_int clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer, cl_uint blocking,
                           size_t offset, size_t size, void *out, cl_uint wait_count,
                           const cl_event *wait, cl_event *event) {
    (void)wait_count; (void)wait; (void)event;
    if (scenario("read_fail")) return CL_INVALID_OPERATION;
    if (queue != &queue_obj || buffer != &buffer_obj || !executed || !blocking ||
        offset != 0 || size != sizeof(results) || !out) return CL_INVALID_OPERATION;
    memcpy(out, results, sizeof(results));
    return CL_SUCCESS;
}

cl_int clReleaseKernel(cl_kernel kernel) { return kernel == &kernel_obj ? CL_SUCCESS : CL_INVALID_VALUE; }
cl_int clReleaseProgram(cl_program program) { return program == &program_obj ? CL_SUCCESS : CL_INVALID_VALUE; }
cl_int clReleaseMemObject(cl_mem mem) {
    if (scenario("release_fail")) return CL_INVALID_OPERATION;
    return mem == &buffer_obj ? CL_SUCCESS : CL_INVALID_VALUE;
}
cl_int clReleaseCommandQueue(cl_command_queue queue) { return queue == &queue_obj ? CL_SUCCESS : CL_INVALID_VALUE; }
cl_int clReleaseContext(cl_context context) { return context == &context_obj ? CL_SUCCESS : CL_INVALID_VALUE; }
