/* Compare ordinary OpenCL transfers with two forms of cross-process shared memory. */
#define _GNU_SOURCE
#define CL_TARGET_OPENCL_VERSION 120
#include "abi.h"
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <android/hardware_buffer.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/dma-buf.h>
#include <linux/memfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FUNCTIONS(X) \
    X(clGetPlatformIDs) X(clGetDeviceIDs) X(clGetDeviceInfo) \
    X(clCreateContext) X(clCreateCommandQueue) X(clCreateBuffer) X(clGetMemObjectInfo) \
    X(clCreateProgramWithSource) X(clBuildProgram) X(clGetProgramBuildInfo) X(clCreateKernel) X(clSetKernelArg) \
    X(clEnqueueWriteBuffer) X(clEnqueueReadBuffer) X(clEnqueueMapBuffer) X(clEnqueueUnmapMemObject) \
    X(clEnqueueNDRangeKernel) X(clFinish) X(clWaitForEvents) X(clGetEventProfilingInfo) \
    X(clReleaseEvent) X(clReleaseKernel) X(clReleaseProgram) X(clReleaseMemObject) \
    X(clReleaseCommandQueue) X(clReleaseContext)

#define FIELD(name) __typeof__(&name) name;
static struct { FUNCTIONS(FIELD) } api;
#undef FIELD

#ifndef RME_CL_PROBE_WIDTH
#define RME_CL_PROBE_WIDTH 1024
#endif
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
enum { WIDTH = RME_CL_PROBE_WIDTH, WORDS = WIDTH * WIDTH, BYTES = WORDS * sizeof(uint32_t), SAMPLES = 24 };
enum { BLUR, PARTICLES };
enum { COPIES, MAPPED, QCOM_AHB };
enum { FILL = 1, VERIFY, STOP };

struct request { uint32_t op, mode, workload, seed, mismatch; };
struct response { uint64_t sync_ns, verified; };
struct buffers { uint32_t input[WORDS], output[WORDS]; };
struct sample { uint64_t transfer, device, wall; };

/* Qualcomm AHB import ABI as used in MNN's OpenCL-Headers and OpenCLBackend.cpp. */
enum { CL_MEM_ANDROID_AHARDWAREBUFFER_HOST_PTR_QCOM = 0x4119 };
struct qcom_ahb_host_ptr { cl_mem_ext_host_ptr ext; AHardwareBuffer *buffer; };

static const char *source =
    "__kernel void blur(__global const uint *in, __global uint *out) {"
    " const uint width=" STRINGIFY(RME_CL_PROBE_WIDTH) ";"
    " uint i=(uint)get_global_id(0), x=i%width;"
    " uint l=x?i-1u:i, r=x+1u<width?i+1u:i;"
    " uint u=i>=width?i-width:i, d=i+width<width*width?i+width:i;"
    " out[i]=(in[l]+in[r]+in[u]+in[d]+in[i]*4u)>>3; }"
    "__kernel void particles(__global const uint *in, __global uint *out) {"
    " uint i=(uint)get_global_id(0), v=in[i];"
    " for(uint j=0;j<16u;++j) { v^=v<<13; v^=v>>17; v^=v<<5; }"
    " out[i]=v; }";

static void check(cl_int status, const char *call) {
    if (!status) return;
    fprintf(stderr, "%s failed (%d)\n", call, status);
    exit(1);
}
#define CL(call) check(api.call, #call)

static void require(int ok, const char *operation) {
    if (ok) return;
    fprintf(stderr, "%s failed\n", operation);
    exit(1);
}

static uint32_t input_value(uint32_t i, uint32_t seed) { return i * UINT32_C(2654435761) + seed; }

static uint32_t expected(unsigned workload, uint32_t i, uint32_t seed) {
    uint32_t v = input_value(i, seed);
    if (workload == PARTICLES) {
        for (unsigned j = 0; j < 16; ++j) { v ^= v << 13; v ^= v >> 17; v ^= v << 5; }
        return v;
    }
    uint32_t x = i % WIDTH;
    uint32_t left = x ? i - 1 : i, right = x + 1 < WIDTH ? i + 1 : i;
    uint32_t up = i >= WIDTH ? i - WIDTH : i, down = i < WORDS - WIDTH ? i + WIDTH : i;
    return (input_value(left, seed) + input_value(right, seed) + input_value(up, seed) +
            input_value(down, seed) + 4 * v) >> 3;
}

static uint64_t now(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static void sync_buffer(int fd, uint64_t flags) {
    struct dma_buf_sync sync = {.flags = flags};
    int status;
    do {
        status = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    } while (status && (errno == EINTR || errno == EAGAIN));
    if (status) { perror("synchronize shared DMA-BUF"); exit(1); }
}

static int consumer(int fd, unsigned candidate) {
    size_t size = 2 * sizeof(struct buffers);
    int backing = rme_cl_read_mapping(fd, size);
    require(backing >= 0, "receive shared baseline backing");
    struct buffers *buffers = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, backing, 0);
    require(buffers != MAP_FAILED, "map shared baseline backing");
    close(backing);
    int dma[2];
    void *maps[2];
    if (candidate == QCOM_AHB) {
        for (unsigned i = 0; i < 2; ++i) {
            dma[i] = rme_cl_read_mapping(fd, BYTES);
            require(dma[i] >= 0, "receive shared DMA-BUF");
            maps[i] = mmap(NULL, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, dma[i], 0);
            require(maps[i] != MAP_FAILED, "map shared DMA-BUF");
        }
    }
    struct request request;
    while (rme_cl_read_exact(fd, &request, sizeof(request)) == 1) {
        if (request.op == STOP) {
            if (candidate == QCOM_AHB) {
                for (unsigned i = 0; i < 2; ++i) {
                    munmap(maps[i], BYTES);
                    close(dma[i]);
                }
            }
            munmap(buffers, size);
            return 0;
        }
        int writing = request.op == FILL;
        struct buffers *memory = buffers + (request.mode != COPIES);
        void *words = writing ? memory->input : memory->output;
        struct response response = {.verified = 1};
        uint64_t access = writing ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
        unsigned index = writing ? 0 : 1;
        if (request.mode == QCOM_AHB) {
            uint64_t start = now();
            sync_buffer(dma[index], DMA_BUF_SYNC_START | access);
            response.sync_ns = now() - start;
            words = maps[index];
        }
        uint32_t *data = words;
        if (request.op == FILL) {
            for (uint32_t i = 0; i < WORDS; ++i) data[i] = input_value(i, request.seed);
        } else if (request.op == VERIFY) {
            for (uint32_t i = 0; i < WORDS; ++i) {
                uint32_t value = expected(request.workload, i, request.seed) ^ (i == 0 && request.mismatch);
                if (data[i] != value) {
                    fprintf(stderr, "MISMATCH mode=%u workload=%u word=%u actual=%u expected=%u\n",
                            request.mode, request.workload, i, data[i], value);
                    response.verified = 0;
                    break;
                }
            }
        } else return 1;
        if (request.mode == QCOM_AHB) {
            uint64_t start = now();
            sync_buffer(dma[index], DMA_BUF_SYNC_END | access);
            response.sync_ns += now() - start;
        }
        if (rme_cl_write_parts(fd, &response, sizeof(response), NULL, 0, NULL, 0)) return 1;
    }
    return 1;
}

static uint64_t request(int fd, uint32_t op, uint32_t mode, uint32_t workload, uint32_t seed, int mismatch) {
    struct request message = {op, mode, workload, seed, (uint32_t)mismatch};
    require(!rme_cl_write_parts(fd, &message, sizeof(message), NULL, 0, NULL, 0), "send consumer command");
    if (op == STOP) return 0;
    struct response response;
    require(rme_cl_read_exact(fd, &response, sizeof(response)) == 1 && response.verified, "cross-process data check");
    return response.sync_ns;
}

static cl_device_id open_driver(const char *path) {
    void *driver = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!driver) { fprintf(stderr, "%s\n", dlerror()); exit(1); }
#define LOAD(name) api.name = (__typeof__(&name))dlsym(driver, #name); require(api.name != NULL, #name);
    FUNCTIONS(LOAD)
#undef LOAD
    cl_platform_id platforms[16];
    cl_uint count = 0;
    CL(clGetPlatformIDs(16, platforms, &count));
    for (cl_uint i = 0; i < count && i < 16; ++i) {
        cl_device_id device;
        if (!api.clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 1, &device, NULL)) return device;
    }
    fputs("No GPU OpenCL device\n", stderr);
    exit(1);
}

static void require_qcom_ahb(cl_device_id device) {
    char extensions[8192];
    CL(clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, sizeof(extensions), extensions, NULL));
    for (char *extension = strtok(extensions, " "); extension; extension = strtok(NULL, " ")) {
        if (!strcmp(extension, "cl_qcom_android_ahardwarebuffer_host_ptr")) return;
    }
    require(0, "device support for cl_qcom_android_ahardwarebuffer_host_ptr");
}

static int is_cached_blob(int fd) {
    if (lseek(fd, 0, SEEK_END) != BYTES) return 0;
    char path[64], line[128], exporter[64];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    FILE *info = fopen(path, "r");
    if (!info) return 0;
    int cached = 0;
    while (fgets(line, sizeof(line), info)) {
        if (sscanf(line, "exp_name: %63s", exporter) == 1) {
            cached = !strcmp(exporter, "qcom,system") || !strcmp(exporter, "system");
            break;
        }
    }
    fclose(info);
    return cached;
}

static void send_blob(AHardwareBuffer *buffer, int consumer_fd) {
    int ends[2];
    require(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends), "open AHB export socket");
    check(AHardwareBuffer_sendHandleToUnixSocket(buffer, ends[0]), "export AHB descriptors");
    char payload[4096];
    union { struct cmsghdr align; char bytes[CMSG_SPACE(32 * sizeof(int))]; } control;
    struct iovec data = {.iov_base = payload, .iov_len = sizeof(payload)};
    struct msghdr message = {.msg_iov = &data, .msg_iovlen = 1, .msg_control = control.bytes,
                             .msg_controllen = sizeof(control.bytes)};
    require(recvmsg(ends[1], &message, MSG_CMSG_CLOEXEC) > 0 &&
            !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)), "receive AHB descriptors");
    close(ends[0]);
    close(ends[1]);
    int blob = -1;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (size_t i = 0; i < count; ++i) {
            int fd;
            memcpy(&fd, CMSG_DATA(header) + i * sizeof(fd), sizeof(fd));
            if (is_cached_blob(fd)) {
                require(blob < 0, "one cached DMA-BUF matches the BLOB size");
                blob = fd;
            } else close(fd);
        }
    }
    require(blob >= 0, "AHB data descriptor uses a cached system heap");
    require(!rme_cl_send_mapping(consumer_fd, blob, BYTES), "send shared DMA-BUF");
    close(blob);
}

static cl_mem create_buffer(cl_context context, unsigned mode, void *address, int fd, AHardwareBuffer **ahb) {
    cl_int status;
    cl_mem_flags flags = CL_MEM_READ_WRITE;
    size_t bytes = BYTES;
    struct qcom_ahb_host_ptr import;
    if (mode == QCOM_AHB) {
        const AHardwareBuffer_Desc desc = {
            .width = BYTES, .height = 1, .layers = 1, .format = AHARDWAREBUFFER_FORMAT_BLOB,
            .usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                     AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
        };
        check(AHardwareBuffer_allocate(&desc, ahb), "allocate shared AHB");
        AHardwareBuffer_Desc actual;
        AHardwareBuffer_describe(*ahb, &actual);
        require(actual.format == desc.format && actual.width == BYTES && actual.stride == BYTES &&
                actual.height == 1 && actual.layers == 1, "linear AHB allocation matches kernel");
        send_blob(*ahb, fd);
        import = (struct qcom_ahb_host_ptr){
            .ext = {CL_MEM_ANDROID_AHARDWAREBUFFER_HOST_PTR_QCOM, CL_MEM_HOST_WRITEBACK_QCOM},
            .buffer = *ahb,
        };
        flags |= CL_MEM_USE_HOST_PTR | CL_MEM_EXT_HOST_PTR_QCOM;
        bytes = 0; /* The Qualcomm AHB import takes the allocation size from the handle. */
        address = &import;
    } else if (mode == MAPPED) {
        flags |= CL_MEM_USE_HOST_PTR;
    } else {
        address = NULL;
    }
    cl_mem memory = api.clCreateBuffer(context, flags, bytes, address, &status);
    check(status, "create OpenCL buffer");
    CL(clGetMemObjectInfo(memory, CL_MEM_SIZE, sizeof(bytes), &bytes, NULL));
    require(bytes == BYTES, "OpenCL buffer matches kernel size");
    return memory;
}

static struct sample run(int fd, cl_command_queue queue, cl_kernel kernel, cl_mem in, cl_mem out,
                         struct buffers *memory, unsigned mode, unsigned workload, uint32_t seed, int mismatch) {
    cl_int status;
    uint64_t start = now(), input_map = 0;
    if (mode == MAPPED) {
        void *mapped = api.clEnqueueMapBuffer(queue, in, CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION, 0, BYTES,
                                              0, NULL, NULL, &status);
        check(status, "map shared input");
        require(mapped == memory->input, "input map aliases shared pages");
        input_map = now() - start;
    }
    uint64_t sync = request(fd, FILL, mode, workload, seed, 0);
    uint64_t after_input = now();
    if (mode == MAPPED) CL(clEnqueueUnmapMemObject(queue, in, memory->input, 0, NULL, NULL));
    else if (mode == COPIES) CL(clEnqueueWriteBuffer(queue, in, CL_TRUE, 0, BYTES, memory->input, 0, NULL, NULL));
    uint64_t transfer = input_map + now() - after_input;

    const size_t work = WORDS, local = 128;
    cl_event event;
    CL(clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &work, &local, 0, NULL, &event));
    CL(clWaitForEvents(1, &event));
    uint64_t read_start = now();
    if (mode == MAPPED) {
        void *mapped = api.clEnqueueMapBuffer(queue, out, CL_TRUE, CL_MAP_READ, 0, BYTES, 0, NULL, NULL, &status);
        check(status, "map shared output");
        require(mapped == memory->output, "output map aliases shared pages");
    } else if (mode == COPIES) CL(clEnqueueReadBuffer(queue, out, CL_TRUE, 0, BYTES, memory->output, 0, NULL, NULL));
    uint64_t before_verify = now();
    transfer += before_verify - read_start;
    sync += request(fd, VERIFY, mode, workload, seed, mismatch);
    uint64_t after_verify = now();
    if (mode == MAPPED) CL(clEnqueueUnmapMemObject(queue, out, memory->output, 0, NULL, NULL));
    CL(clFinish(queue));
    uint64_t cleanup = now() - after_verify;
    cl_ulong gpu_start, gpu_end;
    CL(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(gpu_start), &gpu_start, NULL));
    CL(clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(gpu_end), &gpu_end, NULL));
    CL(clReleaseEvent(event));
    require(gpu_end > gpu_start, "valid device event interval");
    return (struct sample){transfer + cleanup + sync, gpu_end - gpu_start,
                           input_map + before_verify - after_input + cleanup + sync};
}

static int compare_u64(const void *a, const void *b) {
    uint64_t lhs = *(const uint64_t *)a, rhs = *(const uint64_t *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static double median(uint64_t *values) {
    qsort(values, SAMPLES, sizeof(*values), compare_u64);
    return ((double)values[SAMPLES / 2 - 1] + values[SAMPLES / 2]) / 2000.0;
}

static void exec_guest(int fd, const char *root, const char *runner, const char *program) {
    char props[PATH_MAX], descriptor[24];
    int length = snprintf(props, sizeof(props), "%s/dev/__properties__", root);
    require(length > 0 && (size_t)length < sizeof(props), "format Guest properties path");
    snprintf(descriptor, sizeof(descriptor), "%d", fd);
    require(!fcntl(fd, F_SETFD, 0) && !setenv("AEMU64_ROOT", root, 1) && !setenv("AEMU64_PROPS", props, 1) &&
            !setenv("ANDROID_ROOT", "/system", 1) && !setenv("ANDROID_DATA", "/data", 1) &&
            !unsetenv("LD_LIBRARY_PATH"), "prepare Guest runner environment");
    execl(runner, runner, program, program, "--consume", descriptor, (char *)NULL);
    perror("exec Guest runner");
    _exit(1);
}

int main(int argc, char **argv) {
    int mismatch = 0;
    unsigned candidate = MAPPED;
    const char *root = NULL, *runner = NULL, *guest_program = NULL;
    if (argc < 2) return 2;
    if (argc == 3 && !strcmp(argv[1], "--consume")) {
        printf("N64_GUEST_CONSUMER_STARTED\n");
        fflush(stdout);
        return consumer(atoi(argv[2]), QCOM_AHB);
    }
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--expect-mismatch")) mismatch = 1;
        else if (!strcmp(argv[i], "--qcom-ahb")) candidate = QCOM_AHB;
        else if (!strcmp(argv[i], "--guest-runner") && argc - i >= 4) {
            root = argv[++i];
            runner = argv[++i];
            guest_program = argv[++i];
        }
        else return 2;
    }
    if (runner && candidate != QCOM_AHB) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    size_t size = 2 * sizeof(struct buffers);
    int fd = (int)syscall(SYS_memfd_create, "rme-cl-host-ptr-probe", MFD_CLOEXEC);
    require(fd >= 0 && !ftruncate(fd, (off_t)size), "allocate shared backing");
    struct buffers *buffers = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    require(buffers != MAP_FAILED, "map shared backing");
    int ends[2];
    require(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends), "open anonymous control channel");
    pid_t child = fork();
    require(child >= 0, "start consumer");
    if (!child) {
        close(ends[0]);
        close(fd);
        munmap(buffers, size);
        if (runner) exec_guest(ends[1], root, runner, guest_program);
        _exit(consumer(ends[1], candidate));
    }
    close(ends[1]);
    require(!rme_cl_send_mapping(ends[0], fd, size), "send shared baseline backing");
    close(fd);

    cl_device_id device = open_driver(argv[1]);
    if (candidate == QCOM_AHB) require_qcom_ahb(device);
    char name[256];
    CL(clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, NULL));
    printf("DEVICE %s, %u bytes per buffer, %d verified samples per path\n", name, (unsigned)BYTES, SAMPLES);
    cl_int status;
    cl_context context = api.clCreateContext(NULL, 1, &device, NULL, NULL, &status);
    check(status, "create context");
    cl_command_queue queue = api.clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
    check(status, "create profiling queue");
    cl_mem in[2], out[2];
    AHardwareBuffer *input = NULL, *output = NULL;
    for (unsigned index = 0; index < 2; ++index) {
        unsigned mode = index ? candidate : COPIES;
        in[index] = create_buffer(context, mode, buffers[index].input, ends[0], &input);
        out[index] = create_buffer(context, mode, buffers[index].output, ends[0], &output);
    }
    cl_program program = api.clCreateProgramWithSource(context, 1, &source, NULL, &status);
    check(status, "create program");
    status = api.clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (status) {
        char log[8192] = {0};
        api.clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        fprintf(stderr, "%s\n", log);
        check(status, "build program");
    }
    for (unsigned workload = 0; workload < 2; ++workload) {
        const char *kernel_name = workload == BLUR ? "blur" : "particles";
        cl_kernel kernels[2];
        for (unsigned mode = 0; mode < 2; ++mode) {
            kernels[mode] = api.clCreateKernel(program, kernel_name, &status);
            check(status, "create kernel");
            CL(clSetKernelArg(kernels[mode], 0, sizeof(in[mode]), &in[mode]));
            CL(clSetKernelArg(kernels[mode], 1, sizeof(out[mode]), &out[mode]));
        }
        uint64_t results[2][3][SAMPLES];
        for (unsigned round = 0; round < SAMPLES + 4; ++round) {
            for (unsigned i = 0; i < 2; ++i) {
                unsigned index = (round + i) % 2;
                unsigned mode = index ? candidate : COPIES;
                struct sample s = run(ends[0], queue, kernels[index], in[index], out[index], buffers + index,
                                      mode, workload, round + 7, mismatch && index);
                if (round < 4) continue;
                results[index][0][round - 4] = s.transfer;
                results[index][1][round - 4] = s.device;
                results[index][2][round - 4] = s.wall;
            }
        }
        for (unsigned mode = 0; mode < 2; ++mode) {
            printf("%s %s median_us transfer=%.1f device=%.1f ready=%.1f\n", kernel_name,
                   mode ? (candidate == QCOM_AHB ? "qcom-ahb" : "shared-map") : "copy",
                   median(results[mode][0]), median(results[mode][1]),
                   median(results[mode][2]));
            CL(clReleaseKernel(kernels[mode]));
        }
    }
    request(ends[0], STOP, 0, 0, 0, 0);
    close(ends[0]);
    int child_status;
    require(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) && !WEXITSTATUS(child_status),
            "consumer exit");
    CL(clReleaseProgram(program));
    for (unsigned mode = 0; mode < 2; ++mode) {
        CL(clReleaseMemObject(in[mode]));
        CL(clReleaseMemObject(out[mode]));
    }
    CL(clReleaseCommandQueue(queue));
    CL(clReleaseContext(context));
    if (input) AHardwareBuffer_release(input);
    if (output) AHardwareBuffer_release(output);
    munmap(buffers, size);
    puts(runner ? "N64_GUEST_ADRENO_SHARED_BUFFER_ROUNDTRIP_OK" : "ADRENO_SHARED_BUFFER_ROUNDTRIP_OK");
    return 0;
}
