/* Internal calls to the real gfxstream Android native-buffer dispatch. */
#ifndef RME_ANDROID_NATIVE_DRIVER_H
#define RME_ANDROID_NATIVE_DRIVER_H
#include <vulkan/vulkan_core.h>

/* The driver owns fence_fd on every acquire path, including failure. */
VkResult rme_android_driver_acquire(VkDevice device, VkImage image, int fence_fd,
    VkSemaphore semaphore, VkFence fence);
VkResult rme_android_driver_release(VkQueue queue, uint32_t wait_count,
    const VkSemaphore *wait_semaphores, VkImage image, int *release_fence_fd);
#endif
