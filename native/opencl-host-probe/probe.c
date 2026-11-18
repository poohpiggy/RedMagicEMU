/* Debug-only host OpenCL probe. Uses only the public library name and CL 1.2 API. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint32_t cl_bool;
typedef uint64_t cl_bitfield;
typedef intptr_t cl_context_properties;
typedef void *cl_platform_id;
typedef void *cl_device_id;
typedef void *cl_context;
typedef void *cl_command_queue;
typedef void *cl_mem;
typedef void *cl_program;
typedef void *cl_kernel;

enum { CL_SUCCESS = 0, CL_DEVICE_NOT_FOUND = -1, CL_TRUE = 1,
       CL_DEVICE_AVAILABLE = 0x1027, CL_DEVICE_COMPILER_AVAILABLE = 0x1028,
       CL_CONTEXT_PLATFORM = 0x1084, WORDS = 64, MAX_OBJECTS = 1024 };
#define CL_DEVICE_TYPE_GPU ((cl_bitfield)1 << 2)
#define CL_MEM_WRITE_ONLY ((cl_bitfield)1 << 1)

struct api {
    cl_int (*clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
    cl_int (*clGetDeviceIDs)(cl_platform_id, cl_bitfield, cl_uint, cl_device_id *, cl_uint *);
    cl_int (*clGetDeviceInfo)(cl_device_id, cl_uint, size_t, void *, size_t *);
    cl_context (*clCreateContext)(const cl_context_properties *, cl_uint, const cl_device_id *,
                                   void (*)(const char *, const void *, size_t, void *), void *, cl_int *);
    cl_command_queue (*clCreateCommandQueue)(cl_context, cl_device_id, cl_bitfield, cl_int *);
    cl_mem (*clCreateBuffer)(cl_context, cl_bitfield, size_t, void *, cl_int *);
    cl_program (*clCreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
    cl_int (*clBuildProgram)(cl_program, cl_uint, const cl_device_id *, const char *,
                              void (*)(cl_program, void *), void *);
    cl_kernel (*clCreateKernel)(cl_program, const char *, cl_int *);
    cl_int (*clSetKernelArg)(cl_kernel, cl_uint, size_t, const void *);
    cl_int (*clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *,
                                      const size_t *, const size_t *, cl_uint, const void *, void *);
    cl_int (*clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *,
                                   cl_uint, const void *, void *);
    cl_int (*clReleaseKernel)(cl_kernel);
    cl_int (*clReleaseProgram)(cl_program);
    cl_int (*clReleaseMemObject)(cl_mem);
    cl_int (*clReleaseCommandQueue)(cl_command_queue);
    cl_int (*clReleaseContext)(cl_context);
};

int opencl_host_probe(char *result, size_t size) {
    static const char *source = "__kernel void probe(__global uint *out) {"
                                "size_t i = get_global_id(0); out[i] = (uint)i * 3u + 7u; }";
    struct api a = {0};
    /* Keep the driver mapped: a failed enqueue/read may leave vendor work in flight. */
    static void *library;
    cl_platform_id *platforms = NULL, selected_platform = NULL;
    cl_device_id *devices = NULL, device = NULL;
    cl_context context = NULL;
    cl_command_queue queue = NULL;
    cl_mem buffer = NULL;
    cl_program program = NULL;
    cl_kernel kernel = NULL;
    cl_int error = 0;
    int status = 1;
    uint32_t output[WORDS] = {0};
    size_t global = WORDS;
    cl_uint count = 0;
    if (!result || !size) return 1;
    result[0] = '\0';

#define FAIL(...) do { snprintf(result, size, __VA_ARGS__); goto cleanup; } while (0)
#define LOAD(name) do { a.name = dlsym(library, #name); if (!a.name) \
    FAIL("missing OpenCL symbol %s", #name); } while (0)
#define CHECK(call) do { error = (call); if (error != CL_SUCCESS) \
    FAIL("%s: OpenCL error %d", #call, (int)error); } while (0)
#define CREATED(obj, call) do { obj = (call); if (error != CL_SUCCESS || !obj) \
    FAIL("%s: OpenCL error %d%s", #call, (int)error, obj ? "" : " (null)"); } while (0)
#define RELEASE(obj, name) do { if (obj) { error = a.name(obj); \
    if (error != CL_SUCCESS && status == 0) { \
        snprintf(result, size, "%s: OpenCL error %d", #name, (int)error); status = 1; \
    } } } while (0)

    if (!library) library = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { const char *why = dlerror(); FAIL("dlopen libOpenCL.so failed: %.180s", why ? why : "unknown"); }
    LOAD(clGetPlatformIDs); LOAD(clGetDeviceIDs); LOAD(clGetDeviceInfo);
    LOAD(clCreateContext); LOAD(clCreateCommandQueue); LOAD(clCreateBuffer);
    LOAD(clCreateProgramWithSource); LOAD(clBuildProgram); LOAD(clCreateKernel);
    LOAD(clSetKernelArg); LOAD(clEnqueueNDRangeKernel); LOAD(clEnqueueReadBuffer);
    LOAD(clReleaseKernel); LOAD(clReleaseProgram); LOAD(clReleaseMemObject);
    LOAD(clReleaseCommandQueue); LOAD(clReleaseContext);

    CHECK(a.clGetPlatformIDs(0, NULL, &count));
    if (!count || count > MAX_OBJECTS) FAIL("invalid OpenCL platform count %u", count);
    platforms = calloc(count, sizeof(*platforms));
    if (!platforms) FAIL("platform allocation failed");
    CHECK(a.clGetPlatformIDs(count, platforms, NULL));
    for (cl_uint i = 0; i < count && !device; ++i) {
        cl_uint n = 0;
        error = a.clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &n);
        if (error == CL_DEVICE_NOT_FOUND) continue;
        if (error != CL_SUCCESS) FAIL("clGetDeviceIDs: OpenCL error %d", (int)error);
        if (!n || n > MAX_OBJECTS) FAIL("clGetDeviceIDs: invalid GPU count %u", n);
        devices = calloc(n, sizeof(*devices));
        if (!devices) FAIL("GPU allocation failed");
        CHECK(a.clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, n, devices, NULL));
        for (cl_uint j = 0; j < n && !device; ++j) {
            cl_bool available = 0, compiler = 0;
            CHECK(a.clGetDeviceInfo(devices[j], CL_DEVICE_AVAILABLE, sizeof(available), &available, NULL));
            CHECK(a.clGetDeviceInfo(devices[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof(compiler), &compiler, NULL));
            if (available && compiler) { device = devices[j]; selected_platform = platforms[i]; }
        }
        free(devices); devices = NULL;
    }
    if (!device) FAIL("clGetDeviceIDs: no available GPU with an online compiler");

    const cl_context_properties properties[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)selected_platform, 0
    };
    CREATED(context, a.clCreateContext(properties, 1, &device, NULL, NULL, &error));
    CREATED(queue, a.clCreateCommandQueue(context, device, 0, &error));
    CREATED(buffer, a.clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(output), NULL, &error));
    CREATED(program, a.clCreateProgramWithSource(context, 1, &source, NULL, &error));
    CHECK(a.clBuildProgram(program, 1, &device, NULL, NULL, NULL));
    CREATED(kernel, a.clCreateKernel(program, "probe", &error));
    CHECK(a.clSetKernelArg(kernel, 0, sizeof(buffer), &buffer));
    CHECK(a.clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, NULL, 0, NULL, NULL));
    CHECK(a.clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, sizeof(output), output, 0, NULL, NULL));
    for (size_t i = 0; i < WORDS; ++i) {
        uint32_t expected = (uint32_t)i * 3u + 7u;
        if (output[i] != expected)
            FAIL("GPU result mismatch at %zu: got %u expected %u", i, output[i], expected);
    }
    snprintf(result, size, "OpenCL reported GPU: 64/64 values verified");
    status = 0;
cleanup:
    RELEASE(kernel, clReleaseKernel); RELEASE(program, clReleaseProgram);
    RELEASE(buffer, clReleaseMemObject); RELEASE(queue, clReleaseCommandQueue);
    RELEASE(context, clReleaseContext);
    free(devices);
    free(platforms);
    return status;
}
