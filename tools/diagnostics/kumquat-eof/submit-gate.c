/* Test-only: pause the existing Vulkan client immediately before its first
 * vkQueueSubmit. Resume it through an inherited pipe, then call the real loader. */
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count,
                                             const VkSubmitInfo *submits, VkFence fence) {
    PFN_vkQueueSubmit real_submit = (PFN_vkQueueSubmit)dlsym(RTLD_NEXT, "vkQueueSubmit");
    const char *ready = getenv("RME_TEST_READY_FD");
    const char *resume = getenv("RME_TEST_RESUME_FD");
    static int waited;
    if (!real_submit) {
        fprintf(stderr, "FAIL cannot find real vkQueueSubmit\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!waited && (ready || resume)) {
        char byte = 'R';
        waited = 1;
        if (!ready || !resume || write(atoi(ready), &byte, 1) != 1 ||
            read(atoi(resume), &byte, 1) != 1 || byte != 'G') {
            fprintf(stderr, "FAIL Vulkan test gate\n");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return real_submit(queue, count, submits, fence);
}
