/* Standalone OpenCL source-compiler and compute probe; no EMU integration. */
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORDS = 64, LOCAL_SIZE = 16, GROUPS = WORDS / LOCAL_SIZE,
       MAX_PLATFORMS = 1024, MAX_DEVICES = 1024 };

#define CHECK(call) do { cl_int check_error = (call); if (check_error != CL_SUCCESS) { \
    fprintf(stderr, "FAIL %s: OpenCL error %d\n", #call, (int)check_error); goto cleanup; \
} } while (0)
#define RELEASE(handle, call) do { if (handle) { cl_int release_error = call(handle); \
    if (release_error != CL_SUCCESS) { fprintf(stderr, "FAIL %s: OpenCL error %d\n", \
        #call, (int)release_error); result = 1; } } } while (0)

static int rank_device(cl_device_type type) {
    if (type & CL_DEVICE_TYPE_GPU) return 4;
    if (type & CL_DEVICE_TYPE_ACCELERATOR) return 3;
    if (type & CL_DEVICE_TYPE_CPU) return 2;
    return 1;
}

static const char *type_name(cl_device_type type) {
    if (type & CL_DEVICE_TYPE_GPU) return "GPU";
    if (type & CL_DEVICE_TYPE_ACCELERATOR) return "accelerator";
    if (type & CL_DEVICE_TYPE_CPU) return "CPU";
    return "other/unknown";
}

static void print_build_log(cl_program program, cl_device_id device) {
    size_t length = 0;
    cl_int error = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &length);
    if (error != CL_SUCCESS || length == 0 || length > 1024 * 1024) {
        fprintf(stderr, "BUILD_LOG unavailable: error=%d size=%zu\n", (int)error, length);
        return;
    }
    char *log = calloc(length + 1, 1);
    if (!log) { fprintf(stderr, "BUILD_LOG allocation failed\n"); return; }
    error = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, length, log, NULL);
    if (error == CL_SUCCESS) fprintf(stderr, "BUILD_LOG: %s\n", log);
    else fprintf(stderr, "BUILD_LOG unavailable: error=%d\n", (int)error);
    free(log);
}

int main(int argc, char **argv) {
    int bad_expected = 0, bad_kernel = 0, workgroup = 0, result = 1, selected_rank = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--expect-mismatch") && !bad_expected) bad_expected = 1;
        else if (!strcmp(argv[i], "--bad-kernel") && !bad_kernel) bad_kernel = 1;
        else if (!strcmp(argv[i], "--workgroup") && !workgroup) workgroup = 1;
        else { fprintf(stderr, "usage: %s [--workgroup] [--expect-mismatch] | [--bad-kernel]\n", argv[0]); return 2; }
    }
    if (bad_kernel && (bad_expected || workgroup)) {
        fprintf(stderr, "usage: %s [--workgroup] [--expect-mismatch] | [--bad-kernel]\n", argv[0]);
        return 2;
    }

    static const char *const source =
        "__kernel void transform(__global const uint *input, __global uint *output) {\n"
        "  size_t i = get_global_id(0); output[i] = input[i] * 3u + 7u;\n"
        "}\n";
    static const char *const group_source =
        "__kernel void transform(__global const uint *input, __global uint *output) {\n"
        "  __local uint scratch[16];\n"
        "  size_t lid = get_local_id(0);\n"
        "  scratch[lid] = input[get_global_id(0)];\n"
        "  barrier(CLK_LOCAL_MEM_FENCE);\n"
        "  for (uint stride = 8; stride > 0; stride >>= 1) {\n"
        "    if (lid < stride) scratch[lid] += scratch[lid + stride];\n"
        "    barrier(CLK_LOCAL_MEM_FENCE);\n"
        "  }\n"
        "  if (lid == 0) output[get_group_id(0)] = scratch[0];\n"
        "}\n";
    static const char *const invalid_source = "this is not valid OpenCL C source\n";
    cl_platform_id *platforms = NULL;
    cl_device_id *devices = NULL, selected = NULL;
    cl_context context = NULL;
    cl_command_queue queue = NULL;
    cl_mem input_buffer = NULL, output_buffer = NULL;
    cl_program program = NULL;
    cl_kernel kernel = NULL;
    char *name = NULL;
    cl_int error;
    cl_uint count = 0;
    cl_device_type selected_type = 0;
    cl_bool compiler = CL_FALSE;
    uint32_t input[WORDS], output[WORDS];
    size_t global = WORDS;
    for (size_t i = 0; i < WORDS; ++i) { input[i] = (uint32_t)i ^ 0x5au; output[i] = 0; }

    CHECK(clGetPlatformIDs(0, NULL, &count));
    if (!count || count > MAX_PLATFORMS) { fprintf(stderr, "FAIL platform count: %u\n", count); goto cleanup; }
    platforms = calloc(count, sizeof(*platforms));
    if (!platforms) { fprintf(stderr, "FAIL platform allocation\n"); goto cleanup; }
    CHECK(clGetPlatformIDs(count, platforms, NULL));
    for (cl_uint i = 0; i < count; ++i) {
        cl_uint n = 0;
        error = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &n);
        if (error == CL_DEVICE_NOT_FOUND) continue;
        if (error != CL_SUCCESS || n > MAX_DEVICES) {
            fprintf(stderr, "FAIL clGetDeviceIDs count: error=%d count=%u\n", (int)error, n); goto cleanup;
        }
        if (!n) continue;
        devices = calloc(n, sizeof(*devices));
        if (!devices) { fprintf(stderr, "FAIL device allocation\n"); goto cleanup; }
        CHECK(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, n, devices, NULL));
        for (cl_uint j = 0; j < n; ++j) {
            cl_device_type type = 0;
            CHECK(clGetDeviceInfo(devices[j], CL_DEVICE_TYPE, sizeof(type), &type, NULL));
            int rank = rank_device(type);
            if (rank > selected_rank) { selected = devices[j]; selected_type = type; selected_rank = rank; }
        }
        free(devices); devices = NULL;
    }
    if (!selected) { fprintf(stderr, "FAIL no OpenCL device\n"); goto cleanup; }
    size_t name_length = 0;
    CHECK(clGetDeviceInfo(selected, CL_DEVICE_NAME, 0, NULL, &name_length));
    if (!name_length || name_length > 65536) { fprintf(stderr, "FAIL device name length: %zu\n", name_length); goto cleanup; }
    name = calloc(name_length + 1, 1);
    if (!name) { fprintf(stderr, "FAIL device name allocation\n"); goto cleanup; }
    CHECK(clGetDeviceInfo(selected, CL_DEVICE_NAME, name_length, name, NULL));
    CHECK(clGetDeviceInfo(selected, CL_DEVICE_COMPILER_AVAILABLE, sizeof(compiler), &compiler, NULL));
    printf("DEVICE name=%s type=%s type_flags=0x%llx compiler=%s\n", name,
           type_name(selected_type), (unsigned long long)selected_type, compiler ? "yes" : "no");
    if (!compiler) { fprintf(stderr, "FAIL OpenCL source compiler unavailable\n"); goto cleanup; }

    context = clCreateContext(NULL, 1, &selected, NULL, NULL, &error); CHECK(error);
    if (!context) { fprintf(stderr, "FAIL clCreateContext returned null\n"); goto cleanup; }
    queue = clCreateCommandQueue(context, selected, 0, &error); CHECK(error);
    if (!queue) { fprintf(stderr, "FAIL clCreateCommandQueue returned null\n"); goto cleanup; }
    input_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(input), input, &error); CHECK(error);
    if (!input_buffer) { fprintf(stderr, "FAIL input buffer returned null\n"); goto cleanup; }
    output_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(output), NULL, &error); CHECK(error);
    if (!output_buffer) { fprintf(stderr, "FAIL output buffer returned null\n"); goto cleanup; }
    const char *text = bad_kernel ? invalid_source : (workgroup ? group_source : source);
    program = clCreateProgramWithSource(context, 1, &text, NULL, &error); CHECK(error);
    if (!program) { fprintf(stderr, "FAIL program returned null\n"); goto cleanup; }
    error = clBuildProgram(program, 1, &selected, NULL, NULL, NULL);
    if (error != CL_SUCCESS) {
        fprintf(stderr, "FAIL clBuildProgram: OpenCL error %d\n", (int)error);
        print_build_log(program, selected); goto cleanup;
    }
    if (bad_kernel) { fprintf(stderr, "FAIL invalid kernel unexpectedly built\n"); goto cleanup; }
    kernel = clCreateKernel(program, "transform", &error); CHECK(error);
    if (!kernel) { fprintf(stderr, "FAIL kernel returned null\n"); goto cleanup; }
    size_t local = LOCAL_SIZE;
    if (workgroup) {
        size_t device_max = 0, kernel_max = 0;
        cl_ulong device_local = 0, kernel_local = 0;
        cl_uint dimensions = 0;
        CHECK(clGetDeviceInfo(selected, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(device_max), &device_max, NULL));
        CHECK(clGetDeviceInfo(selected, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(device_local), &device_local, NULL));
        CHECK(clGetDeviceInfo(selected, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(dimensions), &dimensions, NULL));
        if (!dimensions || dimensions > 64) { fprintf(stderr, "FAIL work item dimensions=%u\n", dimensions); goto cleanup; }
        size_t *sizes = calloc(dimensions, sizeof(*sizes));
        if (!sizes) { fprintf(stderr, "FAIL work item limits allocation\n"); goto cleanup; }
        error = clGetDeviceInfo(selected, CL_DEVICE_MAX_WORK_ITEM_SIZES, dimensions * sizeof(*sizes), sizes, NULL);
        size_t first_dimension = sizes[0];
        free(sizes);
        CHECK(error);
        CHECK(clGetKernelWorkGroupInfo(kernel, selected, CL_KERNEL_WORK_GROUP_SIZE, sizeof(kernel_max), &kernel_max, NULL));
        CHECK(clGetKernelWorkGroupInfo(kernel, selected, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(kernel_local), &kernel_local, NULL));
        printf("WORKGROUP local=%zu groups=%u elements=%u device_max=%zu dimension0_max=%zu kernel_max=%zu device_local_bytes=%llu kernel_local_bytes=%llu\n",
               local, GROUPS, WORDS, device_max, first_dimension, kernel_max,
               (unsigned long long)device_local, (unsigned long long)kernel_local);
        if (local > device_max || local > first_dimension || local > kernel_max ||
            device_local < LOCAL_SIZE * sizeof(uint32_t) || kernel_local > device_local) {
            fprintf(stderr, "FAIL unsupported workgroup/local memory limits\n"); goto cleanup;
        }
    }
    CHECK(clSetKernelArg(kernel, 0, sizeof(input_buffer), &input_buffer));
    CHECK(clSetKernelArg(kernel, 1, sizeof(output_buffer), &output_buffer));
    CHECK(clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, workgroup ? &local : NULL, 0, NULL, NULL));
    CHECK(clEnqueueReadBuffer(queue, output_buffer, CL_TRUE, 0,
                              workgroup ? GROUPS * sizeof(output[0]) : sizeof(output), output, 0, NULL, NULL));
    if (workgroup) {
        for (size_t group = 0; group < GROUPS; ++group) {
            uint32_t expected = (uint32_t)(bad_expected && group == 0);
            for (size_t i = 0; i < LOCAL_SIZE; ++i) expected += input[group * LOCAL_SIZE + i];
            if (output[group] != expected) {
                fprintf(stderr, "FAIL group[%zu]: got %u expected %u%s\n", group, output[group], expected,
                        bad_expected ? " (intentional expectation mismatch)" : ""); goto cleanup;
            }
            printf("GROUP index=%zu expected=%u actual=%u\n", group, expected, output[group]);
        }
    } else {
        for (size_t i = 0; i < WORDS; ++i) {
            uint32_t expected = input[i] * 3u + 7u + (uint32_t)(bad_expected && i == 0);
            if (output[i] != expected) {
                fprintf(stderr, "FAIL output[%zu]: got %u expected %u%s\n", i, output[i], expected,
                        bad_expected ? " (intentional expectation mismatch)" : ""); goto cleanup;
            }
        }
    }
    if (bad_expected) { fprintf(stderr, "FAIL invalid expectation unexpectedly matched\n"); goto cleanup; }
    result = 0;

cleanup:
    RELEASE(kernel, clReleaseKernel);
    RELEASE(program, clReleaseProgram);
    RELEASE(output_buffer, clReleaseMemObject);
    RELEASE(input_buffer, clReleaseMemObject);
    RELEASE(queue, clReleaseCommandQueue);
    RELEASE(context, clReleaseContext);
    free(name); free(devices); free(platforms);
    if (!result) puts(workgroup ? "OPENCL_WORKGROUP_OK groups=4 local=16 elements=64" :
                                 "OPENCL_COMPUTE_OK words=64");
    return result;
}
