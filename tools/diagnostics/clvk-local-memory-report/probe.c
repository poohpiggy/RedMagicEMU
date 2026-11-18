// SPDX-License-Identifier: MIT
// Query-only regression: no kernel is enqueued.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { cl_int check_error = (expr); if (check_error != CL_SUCCESS) { \
    fprintf(stderr, "ERROR %s=%d\n", #expr, (int)check_error); goto cleanup; } } while (0)

int main(void) {
    static const char *source =
        "__attribute__((noinline)) uint local_read(__local uint* a, uint i) {"
        "  return a[(i + 1) & 15];"
        "}"
        "__kernel void fixed_dynamic(__global uint *out, __local uint *dyn) {"
        "  __local uint fixed[16]; size_t i = get_local_id(0);"
        "  fixed[i] = (uint)i; dyn[i] = (uint)i + 1;"
        "  barrier(CLK_LOCAL_MEM_FENCE);"
        "  out[i] = local_read(fixed, (uint)i) + dyn[i];"
        "}"
        "__kernel void vector_three(__global uint *out) {"
        "  __local uint3 data[7]; size_t i = get_local_id(0);"
        "  if (i < 7) data[i] = (uint3)(i, i + 1, i + 2);"
        "  barrier(CLK_LOCAL_MEM_FENCE);"
        "  if (i < 7) out[i] = data[(i + 1) % 7].y;"
        "}";
    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    cl_context context = NULL;
    cl_program program = NULL;
    cl_kernel first = NULL, second = NULL;
    cl_uint count = 0;
    cl_int error = CL_SUCCESS;
    cl_ulong before = 0, after = 0, vector = 0;
    char name[512] = {0};
    int result = 2;

    CHECK(clGetPlatformIDs(1, &platform, &count));
    if (!count || !platform) { fputs("ERROR no OpenCL platform\n", stderr); goto cleanup; }
    CHECK(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, &count));
    if (!count || !device) { fputs("ERROR no OpenCL device\n", stderr); goto cleanup; }
    CHECK(clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, NULL));
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &error);
    CHECK(error);
    if (!context) { fputs("ERROR null context\n", stderr); goto cleanup; }
    program = clCreateProgramWithSource(context, 1, &source, NULL, &error);
    CHECK(error);
    if (!program) { fputs("ERROR null program\n", stderr); goto cleanup; }
    error = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (error != CL_SUCCESS) {
        size_t size = 0;
        if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &size) == CL_SUCCESS &&
            size > 0 && size < 1024 * 1024) {
            char *log = calloc(size + 1, 1);
            if (log) {
                if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, log, NULL) == CL_SUCCESS)
                    fprintf(stderr, "BUILD_LOG: %s\n", log);
                free(log);
            }
        }
        fprintf(stderr, "ERROR clBuildProgram=%d\n", (int)error);
        goto cleanup;
    }
    first = clCreateKernel(program, "fixed_dynamic", &error);
    CHECK(error);
    if (!first) { fputs("ERROR null first kernel\n", stderr); goto cleanup; }
    second = clCreateKernel(program, "vector_three", &error);
    CHECK(error);
    if (!second) { fputs("ERROR null second kernel\n", stderr); goto cleanup; }
    CHECK(clGetKernelWorkGroupInfo(first, device, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(before), &before, NULL));
    CHECK(clSetKernelArg(first, 1, 96, NULL));
    CHECK(clGetKernelWorkGroupInfo(first, device, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(after), &after, NULL));
    CHECK(clGetKernelWorkGroupInfo(second, device, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(vector), &vector, NULL));
    printf("DEVICE=%s\n", name);
    printf("fixed_dynamic static_min=64 before=%llu dynamic=96 after_min=160 after=%llu\n",
           (unsigned long long)before, (unsigned long long)after);
    printf("vector_three static_min=112 reported=%llu\n", (unsigned long long)vector);
    if (before < 64 || after < 160 || vector < 112) {
        fputs("FAIL CL_KERNEL_LOCAL_MEM_SIZE below required local allocations\n", stderr);
        result = 1;
    } else {
        puts("LOCAL_MEMORY_REPORT_LOWER_BOUNDS_OK");
        result = 0;
    }

cleanup:
    if (second && clReleaseKernel(second) != CL_SUCCESS) result = 2;
    if (first && clReleaseKernel(first) != CL_SUCCESS) result = 2;
    if (program && clReleaseProgram(program) != CL_SUCCESS) result = 2;
    if (context && clReleaseContext(context) != CL_SUCCESS) result = 2;
    return result;
}
