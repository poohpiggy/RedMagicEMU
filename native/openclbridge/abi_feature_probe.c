#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(call) do { cl_int status = (call); if (status) { \
    fprintf(stderr, "%s returned %d at line %d\n", #call, status, __LINE__); return 1; \
} } while (0)

static void CL_CALLBACK complete(cl_event event, cl_int status, void *user) {
    (void)event;
    atomic_store((atomic_int *)user, status);
}

static int transfer(cl_context context, cl_command_queue queue) {
    const size_t bytes = 2 * 1024 * 1024;
    unsigned char *input = malloc(bytes), *output = malloc(bytes);
    if (!input || !output) return 1;
    for (size_t i = 0; i < bytes; ++i) input[i] = (unsigned char)(i * 5 + (i >> 17));
    cl_int error;
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &error);
    CHECK(error);
    for (size_t limit = bytes / 8; limit <= bytes; limit *= 8) {
        CHECK(clEnqueueWriteBuffer(queue, buffer, CL_TRUE, 0, limit, input, 0, NULL, NULL));
        CHECK(clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, limit, output, 0, NULL, NULL));
        if (memcmp(input, output, limit)) return 1;
    }
    CHECK(clReleaseMemObject(buffer));
    free(input);
    free(output);
    if (getenv("RME_CL_REQUIRE_SHARED")) {
        FILE *maps = fopen("/proc/self/maps", "r");
        if (!maps) return 1;
        char line[512];
        int matched = 0;
        while (fgets(line, sizeof(line), maps)) matched |= strstr(line, "memfd:rme-cl-buffer") != NULL;
        fclose(maps);
        if (!matched) { fprintf(stderr, "Large transfers did not use the shared mapping\n"); return 1; }
    }
    return 0;
}

static int event_stream(cl_context context, cl_command_queue queue) {
    for (unsigned i = 0; i < 512; ++i) {
        cl_event event;
        CHECK(clEnqueueMarkerWithWaitList(queue, 0, NULL, &event));
        CHECK(clWaitForEvents(1, &event));
        CHECK(clReleaseEvent(event));
    }
    CHECK(clFinish(queue));
    CHECK(clReleaseCommandQueue(queue));
    CHECK(clReleaseContext(context));
    puts("PUBLIC_OPENCL_EVENT_STREAM_OK");
    return 0;
}

static int async_image_upload(cl_context context, cl_command_queue queue) {
    const size_t top[] = {0, 0, 0}, bottom[] = {0, 64, 0};
    const size_t half[] = {512, 64, 1}, whole[] = {512, 128, 1};
    const size_t row_bytes = 4 * whole[0], pitch = row_bytes + 16;
    unsigned char *input = calloc(whole[1], pitch), *output = calloc(whole[1], pitch);
    if (!input || !output) return 1;
    for (size_t y = 0; y < whole[1]; ++y)
        for (size_t x = 0; x < row_bytes; ++x)
            input[y * pitch + x] = (unsigned char)(y * 29 + x * 7 + 1);

    const cl_image_format format = {CL_RGBA, CL_UNSIGNED_INT8};
    const cl_image_desc description = {
        .image_type = CL_MEM_OBJECT_IMAGE2D, .image_width = whole[0], .image_height = whole[1]
    };
    cl_int error;
    cl_mem image = clCreateImage(context, CL_MEM_READ_WRITE, &format, &description, NULL, &error);
    CHECK(error);
    cl_event event;
    CHECK(clEnqueueWriteImage(queue, image, CL_FALSE, top, half, pitch, 0, input, 0, NULL, &event));
    CHECK(clEnqueueWriteImage(queue, image, CL_FALSE, bottom, half, pitch, 0, input + pitch * half[1],
                              1, &event, NULL));
    CHECK(clReleaseEvent(event));
    CHECK(clFinish(queue));
    CHECK(clEnqueueReadImage(queue, image, CL_TRUE, top, whole, pitch, 0, output, 0, NULL, NULL));
    if (memcmp(input, output, whole[1] * pitch)) {
        fputs("Pending image upload or row padding was overwritten\n", stderr);
        return 1;
    }
    CHECK(clReleaseMemObject(image));
    free(input);
    free(output);
    puts("PUBLIC_OPENCL_ASYNC_IMAGE_UPLOAD_OK");
    return 0;
}

static int async_rect_upload(cl_context context, cl_command_queue queue) {
    const size_t region[] = {1024, 32, 4};
    const size_t origin[][3] = {{3, 1, 1}, {5, 2, 5}};
    const size_t host_origin[][3] = {{7, 1, 1}, {13, 2, 5}};
    const size_t row = 1040, slice = row * 34, bytes = slice * 10;
    const size_t host_row = 1056, host_slice = host_row * 36;
    unsigned char *input = malloc(host_slice * 10), *expected = calloc(1, bytes), *output = calloc(1, bytes);
    if (!input || !expected || !output) return 1;
    memset(input, 0xac, host_slice * 10);
    for (size_t i = 0; i < 2; ++i) {
        for (size_t z = 0; z < region[2]; ++z) {
            for (size_t y = 0; y < region[1]; ++y) {
                size_t src = (host_origin[i][2] + z) * host_slice + (host_origin[i][1] + y) * host_row + host_origin[i][0];
                size_t dst = (origin[i][2] + z) * slice + (origin[i][1] + y) * row + origin[i][0];
                for (size_t x = 0; x < region[0]; ++x)
                    input[src + x] = expected[dst + x] = (unsigned char)(101 * i + 43 * z + 29 * y + 7 * x);
            }
        }
    }

    cl_int error;
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes, output, &error);
    CHECK(error);
    cl_event event;
    CHECK(clEnqueueWriteBufferRect(queue, buffer, CL_FALSE, origin[0], host_origin[0], region,
                                   row, slice, host_row, host_slice, input, 0, NULL, &event));
    CHECK(clEnqueueWriteBufferRect(queue, buffer, CL_FALSE, origin[1], host_origin[1], region,
                                   row, slice, host_row, host_slice, input, 1, &event, NULL));
    CHECK(clReleaseEvent(event));
    CHECK(clFinish(queue));
    CHECK(clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, bytes, output, 0, NULL, NULL));
    if (memcmp(expected, output, bytes)) {
        fputs("Pending rectangular upload or surrounding bytes were overwritten\n", stderr);
        return 1;
    }
    CHECK(clReleaseMemObject(buffer));
    free(input);
    free(expected);
    free(output);
    puts("PUBLIC_OPENCL_ASYNC_RECT_UPLOAD_OK");
    return 0;
}

static int async_upload(cl_context context, cl_command_queue queue) {
    const size_t chunk = 512 * 1024;
    unsigned char *first = malloc(chunk), *second = malloc(chunk), *output = malloc(chunk * 2);
    if (!first || !second || !output) return 1;
    memset(first, 0x39, chunk);
    memset(second, 0xc6, chunk);
    cl_int error;
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, chunk * 2, NULL, &error);
    CHECK(error);
    cl_event event;
    CHECK(clEnqueueWriteBuffer(queue, buffer, CL_FALSE, 0, chunk, first, 0, NULL, &event));
    CHECK(clEnqueueWriteBuffer(queue, buffer, CL_FALSE, chunk, chunk, second, 1, &event, NULL));
    CHECK(clReleaseEvent(event));
    CHECK(clFinish(queue));
    CHECK(clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, chunk * 2, output, 0, NULL, NULL));
    if (memcmp(first, output, chunk) || memcmp(second, output + chunk, chunk)) {
        fputs("Pending upload was overwritten by a later request\n", stderr);
        return 1;
    }
    CHECK(clReleaseMemObject(buffer));
    if (async_image_upload(context, queue) || async_rect_upload(context, queue)) return 1;
    CHECK(clReleaseCommandQueue(queue));
    CHECK(clReleaseContext(context));
    free(first);
    free(second);
    free(output);
    puts("PUBLIC_OPENCL_ASYNC_UPLOAD_OK");
    return 0;
}

static int async_read(cl_context context, cl_command_queue queue) {
    const size_t chunk = 512 * 1024;
    const size_t slice = chunk / 4;
    unsigned char *input = malloc(2 * chunk), *output = malloc(2 * chunk);
    if (!input || !output) return 1;
    for (size_t i = 0; i < 2 * chunk; i += slice)
        memset(input + i, (int)(0x39 + i / slice * 17), slice);
    cl_int error;
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 2 * chunk, input, &error);
    CHECK(error);

    cl_event started, done;
    CHECK(clEnqueueReadBuffer(queue, buffer, CL_FALSE, chunk, chunk, output, 0, NULL, &started));
    cl_int execution;
    CHECK(clGetEventInfo(started, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(execution), &execution, NULL));
    if (getenv("RME_CL_EXPECT_READ_GATED") && execution <= CL_COMPLETE) return 1;
    CHECK(clEnqueueReadBuffer(queue, buffer, CL_FALSE, 0, chunk, output + chunk, 1, &started, &done));
    CHECK(clReleaseEvent(started));
    CHECK(clWaitForEvents(1, &done));
    CHECK(clReleaseEvent(done));
    if (memcmp(output, input + chunk, chunk) || memcmp(output + chunk, input, chunk)) return 1;

    CHECK(clEnqueueReadBuffer(queue, buffer, CL_FALSE, 0, 2 * chunk, output, 0, NULL, NULL));
    CHECK(clFinish(queue));
    if (memcmp(output, input, 2 * chunk)) return 1;

    CHECK(clEnqueueReadBuffer(queue, buffer, CL_FALSE, chunk, chunk, output, 0, NULL, &done));
    CHECK(clFlush(queue));
    const struct timespec interval = {.tv_nsec = 1000000};
    for (unsigned i = 0; i < 2000; ++i) {
        CHECK(clGetEventInfo(done, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(execution), &execution, NULL));
        if (execution <= CL_COMPLETE) break;
        nanosleep(&interval, NULL);
    }
    if (execution != CL_COMPLETE || memcmp(output, input + chunk, chunk)) return 1;
    CHECK(clReleaseEvent(done));

    /* Keep more reads in flight than there are shared slots. */
    unsigned char *many = malloc(5 * slice);
    if (!many) return 1;
    for (size_t i = 0; i < 5; ++i)
        CHECK(clEnqueueReadBuffer(queue, buffer, CL_FALSE, i * slice, slice,
                                  many + i * slice, 0, NULL, NULL));
    CHECK(clFinish(queue));
    for (size_t i = 0; i < 5; ++i)
        if (memcmp(many + i * slice, input + i * slice, slice)) return 1;
    free(many);
    CHECK(clReleaseMemObject(buffer));
    CHECK(clReleaseCommandQueue(queue));
    CHECK(clReleaseContext(context));
    free(input);
    free(output);
    puts("PUBLIC_OPENCL_ASYNC_READ_OK");
    return 0;
}

static int fp16(cl_device_id device, cl_context context, cl_command_queue queue, int required) {
    static const char extension[] = "cl_khr_fp16";
    size_t size;
    CHECK(clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, NULL, &size));
    char extensions[sizeof(extension)];
    CHECK(clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, sizeof(extensions), extensions, NULL));
    cl_device_fp_config config = 0;
    cl_int config_status = clGetDeviceInfo(device, CL_DEVICE_HALF_FP_CONFIG, sizeof(config), &config, NULL);
    if (!required) {
        if (size != 1 || extensions[0] || (config_status != CL_INVALID_VALUE && (config_status || config))) return 1;
        puts("PUBLIC_OPENCL_FP16_UNSUPPORTED_OK");
        return 0;
    }
    if (size != sizeof(extension) || strcmp(extensions, extension)) {
        fprintf(stderr, "Missing FP16 extension (list size=%zu, half config status=%d, flags=%llu)\n",
                size, config_status, (unsigned long long)config);
        return 1;
    }
    CHECK(config_status);
    if (!config || clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, size - 1, extensions, NULL) != CL_INVALID_VALUE)
        return 1;
    static const char *source =
        "#pragma OPENCL EXTENSION cl_khr_fp16 : enable\n"
        "__kernel void fp16(__global const half4 *in, __global half4 *out) {"
        " uint i=(uint)get_global_id(0);"
        " out[i]=in[i]*(half)0.5f+(half)1.0f; }";
    cl_int error;
    cl_program program = clCreateProgramWithSource(context, 1, &source, NULL, &error);
    CHECK(error);
    CHECK(clBuildProgram(program, 1, &device, "-cl-std=CL1.2", NULL, NULL));
    cl_kernel kernel = clCreateKernel(program, "fp16", &error);
    CHECK(error);
    uint16_t input[4][4] = {
        {0x0000, 0x4000, 0x4400, 0x4800}, {0x3c00, 0x4000, 0x4400, 0x4800},
        {0x4000, 0x4000, 0x4400, 0x4800}, {0x4200, 0x4000, 0x4400, 0x4800},
    };
    const uint16_t expected[4][4] = {
        {0x3c00, 0x4000, 0x4200, 0x4500}, {0x3e00, 0x4000, 0x4200, 0x4500},
        {0x4000, 0x4000, 0x4200, 0x4500}, {0x4100, 0x4000, 0x4200, 0x4500},
    };
    uint16_t output[4][4];
    cl_mem values = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(input), input, &error);
    CHECK(error);
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(output), NULL, &error);
    CHECK(error);
    CHECK(clSetKernelArg(kernel, 0, sizeof(values), &values));
    CHECK(clSetKernelArg(kernel, 1, sizeof(buffer), &buffer));
    const size_t work = 4;
    CHECK(clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &work, NULL, 0, NULL, NULL));
    CHECK(clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, sizeof(output), output, 0, NULL, NULL));
    if (memcmp(expected, output, sizeof(output))) { fputs("FP16 output mismatch\n", stderr); return 1; }
    CHECK(clReleaseMemObject(buffer));
    CHECK(clReleaseMemObject(values));
    CHECK(clReleaseKernel(kernel));
    CHECK(clReleaseProgram(program));
    printf("PUBLIC_OPENCL_FP16_OK words=16 config=0x%llx\n", (unsigned long long)config);
    return 0;
}

int main(int argc, char **argv) {
    static const char *source[] = {
        "__kernel void pack(read_only image2d_t image, sampler_t sampler, __global uint *out,",
        "__local uint *tmp) { int x=get_global_id(0), y=get_global_id(1);"
        " uint v=read_imageui(image,sampler,(int2)(x,y)).y; size_t i=get_local_id(0);"
        " tmp[i]=v; barrier(CLK_LOCAL_MEM_FENCE); out[y*8+x]=tmp[i]+3; }",
    };
    cl_platform_id platform;
    cl_device_id device;
    cl_int error;
    CHECK(clGetPlatformIDs(1, &platform, NULL));
    CHECK(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL));
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
    cl_context context = clCreateContext(properties, 1, &device, NULL, NULL, &error);
    CHECK(error);
    cl_context_properties returned[3];
    CHECK(clGetContextInfo(context, CL_CONTEXT_PROPERTIES, sizeof(returned), returned, NULL));
    if (memcmp(returned, properties, sizeof(properties))) return 1;
    cl_command_queue queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &error);
    CHECK(error);
    if (argc == 2 && !strcmp(argv[1], "--event-stream")) return event_stream(context, queue);
    if (argc == 2 && !strcmp(argv[1], "--async-upload")) return async_upload(context, queue);
    if (argc == 2 && !strcmp(argv[1], "--async-read")) return async_read(context, queue);
    if (argc == 2 && (!strcmp(argv[1], "--fp16") || !strcmp(argv[1], "--no-fp16"))) {
        int status = fp16(device, context, queue, !strcmp(argv[1], "--fp16"));
        CHECK(clReleaseCommandQueue(queue));
        CHECK(clReleaseContext(context));
        return status;
    }
    cl_command_queue second = clCreateCommandQueue(context, device, 0, &error);
    CHECK(error);
    if (transfer(context, second)) return 1;
    cl_uint formats = 0;
    CHECK(clGetSupportedImageFormats(context, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, NULL, &formats));
    if (!formats) { puts("PUBLIC_OPENCL_IMAGES_UNAVAILABLE"); return 2; }

    cl_image_format format = {CL_RGBA, CL_UNSIGNED_INT8};
    cl_image_desc description = {.image_type = CL_MEM_OBJECT_IMAGE2D, .image_width = 8, .image_height = 4};
    cl_mem image = clCreateImage(context, CL_MEM_READ_WRITE, &format, &description, NULL, &error);
    CHECK(error);
    const size_t origin[] = {0, 0, 0}, region[] = {8, 4, 1};
    unsigned char rows[4][40] = {{0}};
    for (unsigned i = 0; i < 32; ++i) rows[i / 8][(i % 8) * 4 + 1] = (unsigned char)(i * 3);
    cl_event upload;
    CHECK(clEnqueueWriteImage(queue, image, CL_TRUE, origin, region, sizeof(rows[0]), 0, rows, 0, NULL, &upload));
    CHECK(clRetainEvent(upload));
    cl_uint references;
    CHECK(clGetEventInfo(upload, CL_EVENT_REFERENCE_COUNT, sizeof(references), &references, NULL));
    if (references < 2) return 1;
    CHECK(clReleaseEvent(upload));
    unsigned char copy[4][40];
    memset(copy, 0xa7, sizeof(copy));
    CHECK(clEnqueueReadImage(queue, image, CL_TRUE, origin, region, sizeof(copy[0]), 0, copy, 0, NULL, NULL));
    for (unsigned y = 0; y < 4; ++y) {
        if (memcmp(rows[y], copy[y], 32) || copy[y][32] != 0xa7) return 1;
    }

    cl_sampler sampler = clCreateSampler(context, CL_FALSE, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_NEAREST, &error);
    CHECK(error);
    uint32_t backing[32] = {0};
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, sizeof(backing), backing, &error);
    CHECK(error);
    void *host_pointer;
    CHECK(clGetMemObjectInfo(buffer, CL_MEM_HOST_PTR, sizeof(host_pointer), &host_pointer, NULL));
    if (host_pointer != backing) return 1;
    cl_program program = clCreateProgramWithSource(context, 2, (const char **)source, NULL, &error);
    CHECK(error);
    CHECK(clBuildProgram(program, 0, NULL, "-cl-std=CL1.2", NULL, NULL));
    cl_kernel kernel = clCreateKernel(program, "pack", &error);
    CHECK(error);
    CHECK(clSetKernelArg(kernel, 0, sizeof(image), &image));
    CHECK(clSetKernelArg(kernel, 1, sizeof(sampler), &sampler));
    CHECK(clSetKernelArg(kernel, 2, sizeof(buffer), &buffer));
    CHECK(clSetKernelArg(kernel, 3, 4 * sizeof(uint32_t), NULL));
    const size_t global[] = {8, 4}, local[] = {4, 1};
    cl_event executed;
    CHECK(clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global, local, 1, &upload, &executed));
    atomic_int completion = CL_QUEUED;
    CHECK(clSetEventCallback(executed, CL_COMPLETE, complete, &completion));
    CHECK(clFlush(queue));
    cl_event mapped;
    uint32_t *values = clEnqueueMapBuffer(second, buffer, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE,
                                          0, 32 * sizeof(*values), 1, &executed, &mapped, &error);
    CHECK(error);
    cl_uint map_count;
    CHECK(clGetMemObjectInfo(buffer, CL_MEM_MAP_COUNT, sizeof(map_count), &map_count, NULL));
    if (values != backing || map_count != 1) return 1;
    for (unsigned i = 0; i < 32; ++i) {
        if (values[i] != i * 3 + 3) return 1;
        values[i] += 4;
    }
    cl_event unmapped;
    CHECK(clEnqueueUnmapMemObject(second, buffer, values, 1, &mapped, &unmapped));
    uint32_t result[32];
    CHECK(clEnqueueReadBuffer(second, buffer, CL_TRUE, 0, sizeof(result), result, 1, &unmapped, NULL));
    for (unsigned i = 0; i < 32; ++i) if (result[i] != i * 3 + 7) return 1;
    cl_ulong start, end;
    CHECK(clGetEventProfilingInfo(executed, CL_PROFILING_COMMAND_START, sizeof(start), &start, NULL));
    CHECK(clGetEventProfilingInfo(executed, CL_PROFILING_COMMAND_END, sizeof(end), &end, NULL));
    if (end <= start) return 1;
    cl_command_queue owner;
    CHECK(clGetEventInfo(executed, CL_EVENT_COMMAND_QUEUE, sizeof(owner), &owner, NULL));
    if (owner != queue) return 1;
    const struct timespec interval = {.tv_nsec = 1000000};
    for (int i = 0; i < 1000 && atomic_load(&completion) == CL_QUEUED; ++i) nanosleep(&interval, NULL);
    if (atomic_load(&completion) != CL_COMPLETE) return 1;

    const size_t rect[] = {8, 2, 1}, buffer_origin[] = {4, 0, 0}, host_origin[] = {4, 0, 0};
    uint32_t padded[2][4] = {{0}};
    CHECK(clEnqueueReadBufferRect(second, buffer, CL_TRUE, buffer_origin, host_origin, rect,
                                  32, 0, sizeof(padded[0]), 0, padded, 0, NULL, NULL));
    if (padded[0][0] || padded[0][1] != 10 || padded[0][2] != 13 || padded[0][3] ||
        padded[1][0] || padded[1][1] != 34 || padded[1][2] != 37 || padded[1][3]) return 1;

    size_t binary_size;
    CHECK(clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(binary_size), &binary_size, NULL));
    if (!binary_size) return 1;
    unsigned char *binary = malloc(binary_size);
    if (!binary) return 1;
    CHECK(clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(binary), &binary, NULL));
    const unsigned char *binary_input = binary;
    cl_int binary_status;
    cl_program reloaded = clCreateProgramWithBinary(context, 1, &device, &binary_size, &binary_input, &binary_status, &error);
    CHECK(error);
    CHECK(binary_status);
    free(binary);
    CHECK(clBuildProgram(reloaded, 0, NULL, NULL, NULL, NULL));
    CHECK(clReleaseProgram(reloaded));

    cl_bool linker = CL_FALSE;
    CHECK(clGetDeviceInfo(device, CL_DEVICE_LINKER_AVAILABLE, sizeof(linker), &linker, NULL));
    if (linker) {
        const char *text = "__kernel void linked(__global uint *out) { out[0] = 123; }";
        cl_program object = clCreateProgramWithSource(context, 1, &text, NULL, &error);
        CHECK(error);
        CHECK(clCompileProgram(object, 0, NULL, "-cl-std=CL1.2", 0, NULL, NULL, NULL, NULL));
        cl_program linked = clLinkProgram(context, 0, NULL, NULL, 1, &object, NULL, NULL, &error);
        CHECK(error);
        cl_kernel single;
        cl_uint amount;
        CHECK(clCreateKernelsInProgram(linked, 1, &single, &amount));
        if (amount != 1) return 1;
        CHECK(clSetKernelArg(single, 0, sizeof(buffer), &buffer));
        CHECK(clEnqueueTask(second, single, 0, NULL, NULL));
        CHECK(clEnqueueReadBuffer(second, buffer, CL_TRUE, 0, sizeof(result[0]), result, 0, NULL, NULL));
        if (result[0] != 123) return 1;
        CHECK(clReleaseKernel(single)); CHECK(clReleaseProgram(linked)); CHECK(clReleaseProgram(object));
        puts("PUBLIC_OPENCL_COMPILE_LINK_OK");
    }

    CHECK(clReleaseEvent(unmapped)); CHECK(clReleaseEvent(mapped)); CHECK(clReleaseEvent(executed)); CHECK(clReleaseEvent(upload));
    if (clReleaseEvent(upload) != CL_INVALID_EVENT) return 1;
    CHECK(clReleaseKernel(kernel)); CHECK(clReleaseProgram(program)); CHECK(clReleaseSampler(sampler));
    CHECK(clReleaseMemObject(buffer)); CHECK(clReleaseMemObject(image));
    CHECK(clReleaseCommandQueue(second)); CHECK(clReleaseCommandQueue(queue)); CHECK(clReleaseContext(context));
    puts("PUBLIC_OPENCL_IMAGE_MAP_EVENT_BINARY_OK");
    return 0;
}
