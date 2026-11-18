/* Host-only Vulkan device lifetime and fixed parent-pipe compute command. */
#define _POSIX_C_SOURCE 200809L
#include "gpu_device.h"
#include "host_fixed_compute.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum { RME_VK_IO_TIMEOUT_MS = 2000, RME_VK_FIXED_BYTES = RME_VK_FIXED_WORDS * 4 };

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* One deadline covers the whole frame, including fragmented I/O and EINTR. */
static int transfer_frame(int fd, unsigned char *data, size_t length, int writing) {
    int64_t now = monotonic_ms();
    if (now < 0) return -1;
    const int64_t deadline = now + RME_VK_IO_TIMEOUT_MS;
    int flags = -1;
    if (writing) {
        flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    }
    size_t done = 0;
    int result = -1;
    while (done < length) {
        now = monotonic_ms();
        if (now < 0 || now >= deadline) break;
        struct pollfd event = { .fd=fd, .events=(short)(writing ? POLLOUT : POLLIN) };
        int ready = poll(&event, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || (event.revents & (POLLERR | POLLNVAL))) break;
        if (!(event.revents & (writing ? POLLOUT : (POLLIN | POLLHUP)))) break;
        now = monotonic_ms();
        if (now < 0 || now >= deadline) break;
        ssize_t count = writing ? write(fd, data + done, length - done) : read(fd, data + done, length - done);
        if (count > 0) { done += (size_t)count; continue; }
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;
    }
    now = monotonic_ms();
    if (done == length && now >= 0 && now < deadline) result = 0;
    if (writing && fcntl(fd, F_SETFL, flags) < 0) result = -1;
    return result;
}

static uint32_t get_le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put_le32(unsigned char *p, uint32_t value) {
    p[0]=(unsigned char)value; p[1]=(unsigned char)(value >> 8);
    p[2]=(unsigned char)(value >> 16); p[3]=(unsigned char)(value >> 24);
}

static int write_ready(void) {
    static const char text[] = "RME_VK_HOST_READY\n";
    size_t off = 0;
    while (off < sizeof(text) - 1) {
        ssize_t n = write(STDOUT_FILENO, text + off, sizeof(text) - 1 - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int watch_parent(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "--parent-pid") || !argv[2][0]) return -1;
    for (const char *p = argv[2]; *p; ++p) if (*p < '0' || *p > '9') return -1;
    errno = 0;
    char *end;
    long pid = strtol(argv[2], &end, 10);
    if (errno || *end || pid <= 1 || pid > INT_MAX) return -1;
    /* Install first, then compare: if the expected parent died earlier and we
       were reparented, this check rejects the new parent. If it dies later,
       the kernel delivers SIGKILL even while a Vulkan driver call is stuck. */
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != (pid_t)pid) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (watch_parent(argc, argv)) {
        fprintf(stderr, "RME_VK_HOST_PARENT_FAILED\n");
        return 1;
    }
    /* A closed parent output pipe is a failure, not an unclean SIGPIPE exit. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return 1;
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="RedMagicEMU Vulkan host", .apiVersion=VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ci = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&ci, NULL, &instance);
    if (result != VK_SUCCESS || !instance) {
        fprintf(stderr, "RME_VK_HOST_INSTANCE_FAILED result=%d\n", (int)result);
        return 1;
    }
    struct rme_vk_gpu_device gpu = {0};
    enum rme_vk_device_status status = rme_vk_gpu_open(instance, vkGetInstanceProcAddr, &gpu);
    if (status != RME_VK_DEVICE_OK) {
        fprintf(stderr, "RME_VK_HOST_DEVICE_FAILED status=%d select=%d result=%d\n",
                (int)status, (int)gpu.select_status, (int)gpu.vk_result);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    int exit_code = 0;
    if (write_ready()) {
        fprintf(stderr, "RME_VK_HOST_READY_FAILED\n");
        exit_code = 1;
    } else {
        for (;;) {
            unsigned char control = 0;
            ssize_t n;
            do { n = read(STDIN_FILENO, &control, 1); } while (n < 0 && errno == EINTR);
            if (n < 0) { fprintf(stderr, "RME_VK_HOST_CONTROL_READ_FAILED\n"); exit_code = 1; break; }
            if (n == 0 || control == 'Q') break;
            if (control != 'C') { fprintf(stderr, "RME_VK_HOST_INVALID_CONTROL\n"); exit_code = 2; break; }
            unsigned char wire[RME_VK_FIXED_BYTES];
            uint32_t input[RME_VK_FIXED_WORDS], output[RME_VK_FIXED_WORDS];
            if (transfer_frame(STDIN_FILENO, wire, sizeof(wire), 0)) {
                fprintf(stderr, "RME_VK_HOST_REQUEST_FAILED\n"); exit_code = 1; break;
            }
            for (unsigned i=0; i<RME_VK_FIXED_WORDS; ++i) input[i] = get_le32(wire + 4 * i);
            enum rme_vk_fixed_status status = rme_vk_host_fixed_compute(instance, &gpu, input, output);
            if (status == RME_VK_FIXED_UNRESOLVED) {
                /* Never destroy resources whose GPU completion is unknown. */
                fprintf(stderr, "RME_VK_HOST_COMPUTE_UNRESOLVED\n");
                fflush(stderr);
                _exit(1);
            }
            if (status != RME_VK_FIXED_OK) {
                fprintf(stderr, "RME_VK_HOST_COMPUTE_FAILED\n"); exit_code = 1; break;
            }
            unsigned char reply[4 + RME_VK_FIXED_BYTES] = { 'R','M','C','1' };
            for (unsigned i=0; i<RME_VK_FIXED_WORDS; ++i) put_le32(reply + 4 + 4 * i, output[i]);
            if (transfer_frame(STDOUT_FILENO, reply, sizeof(reply), 1)) {
                fprintf(stderr, "RME_VK_HOST_RESPONSE_FAILED\n"); exit_code = 1; break;
            }
        }
    }
    if (rme_vk_gpu_close(&gpu) != RME_VK_DEVICE_OK) {
        fprintf(stderr, "RME_VK_HOST_CLOSE_FAILED\n");
        return 1; /* Keep the instance alive until process exit if device cleanup failed. */
    }
    vkDestroyInstance(instance, NULL);
    return exit_code;
}
