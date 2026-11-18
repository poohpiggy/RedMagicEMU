/* Internal one-image flow, unavailable to applications until full WSI exists. */
#ifndef RME_ANDROID_SWAPCHAIN_FLOW_H
#define RME_ANDROID_SWAPCHAIN_FLOW_H
#include "android_buffer_queue.h"
#include "android_swapchain_slots.h"

struct RmeAndroidDriverOps {
    int (*dup_fd)(int);
    int (*close_fd)(int);
    /* Takes ownership of fence_fd on both success and failure. */
    VkResult (*acquire)(VkDevice, VkImage, int, VkSemaphore, VkFence);
    VkResult (*release)(VkQueue, uint32_t, const VkSemaphore *, VkImage, int *);
};
VkResult rme_swapchain_acquire(struct ANativeWindow *, const struct RmeAndroidQueueOps *,
    const struct RmeAndroidDriverOps *, struct RmeSwapchainSlot *, uint32_t, VkDevice,
    uint64_t, VkSemaphore, VkFence, uint32_t *);
VkResult rme_swapchain_present(struct ANativeWindow *, const struct RmeAndroidQueueOps *,
    const struct RmeAndroidDriverOps *, struct RmeSwapchainSlot *, uint32_t, VkQueue,
    uint32_t index, uint32_t wait_count, const VkSemaphore *wait_semaphores);
const struct RmeAndroidDriverOps *rme_android_driver_ops(void);
#endif
