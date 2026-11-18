/* Internal ownership flow based on Android 16 Vulkan swapchain behavior.
 * Copyright 2015 The Android Open Source Project, Apache-2.0. */
#include "android_swapchain_flow.h"

static void cancel_slot(struct ANativeWindow *window, const struct RmeAndroidQueueOps *ops,
    struct RmeSwapchainSlot *slots, uint32_t count, uint32_t index) {
    int fd;
    if (rme_slot_release(slots, count, index, &fd) == VK_SUCCESS)
        (void)rme_android_cancel(window, ops, slots[index].buffer, fd);
}

VkResult rme_swapchain_acquire(struct ANativeWindow *window, const struct RmeAndroidQueueOps *queue_ops,
    const struct RmeAndroidDriverOps *driver, struct RmeSwapchainSlot *slots, uint32_t count,
    VkDevice device, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *out_index) {
    struct ANativeWindowBuffer *buffer;
    int original_fd = -1;
    VkResult result = rme_android_dequeue(window, queue_ops, timeout, &buffer, &original_fd);
    if (result != VK_SUCCESS) return result;
    uint32_t index;
    result = rme_slot_claim(slots, count, buffer, original_fd, &index);
    if (result != VK_SUCCESS) {
        (void)rme_android_cancel(window, queue_ops, buffer, original_fd);
        return result;
    }
    int driver_fd = -1;
    if (original_fd >= 0) {
        driver_fd = driver->dup_fd(original_fd);
        if (driver_fd < 0) {
            cancel_slot(window, queue_ops, slots, count, index);
            return VK_ERROR_DEVICE_LOST;
        }
    }
    /* The acquire driver consumes the duplicate even when it fails. */
    result = driver->acquire(device, slots[index].image, driver_fd, semaphore, fence);
    if (result != VK_SUCCESS) {
        cancel_slot(window, queue_ops, slots, count, index);
        return result;
    }
    *out_index = index;
    return VK_SUCCESS;
}

VkResult rme_swapchain_present(struct ANativeWindow *window, const struct RmeAndroidQueueOps *queue_ops,
    const struct RmeAndroidDriverOps *driver, struct RmeSwapchainSlot *slots, uint32_t count,
    VkQueue queue, uint32_t index, uint32_t wait_count, const VkSemaphore *wait_semaphores) {
    if (index >= count || !slots[index].acquired) return VK_ERROR_OUT_OF_DATE_KHR;
    int release_fd = -1;
    VkResult result = driver->release(queue, wait_count, wait_semaphores, slots[index].image, &release_fd);
    if (result != VK_SUCCESS) {
        if (release_fd >= 0) driver->close_fd(release_fd);
        return result;
    }
    /* queueBuffer takes the release fd even on error. */
    result = rme_android_queue(window, queue_ops, slots[index].buffer, release_fd);
    if (result != VK_SUCCESS) return result;
    int original_fd;
    result = rme_slot_release(slots, count, index, &original_fd);
    if (result == VK_SUCCESS && original_fd >= 0) driver->close_fd(original_fd);
    return result;
}
