/* Fake for framing tests only. Never compiled into the Android host service. */
#include "../host_fixed_compute.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum rme_vk_fixed_status rme_vk_host_fixed_compute(VkInstance instance,
    struct rme_vk_gpu_device *gpu, const uint32_t input[RME_VK_FIXED_WORDS],
    uint32_t output[RME_VK_FIXED_WORDS]) {
    (void)instance; (void)gpu;
    const char *mode = getenv("RME_VK_FAKE_CASE");
    if (mode && !strcmp(mode, "compute_fail")) return RME_VK_FIXED_FAILED;
    if (mode && !strcmp(mode, "compute_unresolved")) return RME_VK_FIXED_UNRESOLVED;
    if (mode && !strcmp(mode, "compute_unresolved_stderr_full")) {
        int flags = fcntl(STDERR_FILENO, F_GETFL);
        if (flags < 0 || fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) _exit(3);
        char data[1024]; memset(data, 'x', sizeof(data));
        for (;;) {
            ssize_t n = write(STDERR_FILENO, data, sizeof(data));
            if (n > 0 || (n < 0 && errno == EINTR)) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            _exit(3);
        }
        if (fcntl(STDERR_FILENO, F_SETFL, flags) < 0) _exit(3);
        return RME_VK_FIXED_UNRESOLVED;
    }
    for (unsigned i=0; i<RME_VK_FIXED_WORDS; ++i) output[i] = input[i] * 3u + 7u;
    return RME_VK_FIXED_OK;
}
