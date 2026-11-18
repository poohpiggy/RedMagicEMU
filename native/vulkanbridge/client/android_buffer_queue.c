/* Android queue error and timeout behavior follows the Android 16 Vulkan
 * loader's swapchain implementation, Copyright 2015 The Android Open Source
 * Project, licensed under the Apache License, Version 2.0. */
#include "android_buffer_queue.h"
#include <errno.h>
#include <stdint.h>

VkResult rme_android_dequeue(struct ANativeWindow *window, const struct RmeAndroidQueueOps *ops,
    uint64_t timeout, struct ANativeWindowBuffer **buffer, int *fence_fd) {
    int64_t ns = timeout > INT64_MAX ? -1 : (int64_t)timeout;
    if (ops->set_timeout(window, ns) != 0) return VK_ERROR_SURFACE_LOST_KHR;
    int result = ops->dequeue(window, buffer, fence_fd);
    if (result == -ETIMEDOUT || result == -ENOSYS) return timeout ? VK_TIMEOUT : VK_NOT_READY;
    if (result != 0) return VK_ERROR_SURFACE_LOST_KHR;
    return VK_SUCCESS;
}

VkResult rme_android_queue(struct ANativeWindow *window, const struct RmeAndroidQueueOps *ops,
    struct ANativeWindowBuffer *buffer, int fence_fd) {
    return ops->queue(window, buffer, fence_fd) == 0 ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
}

VkResult rme_android_cancel(struct ANativeWindow *window, const struct RmeAndroidQueueOps *ops,
    struct ANativeWindowBuffer *buffer, int fence_fd) {
    return ops->cancel(window, buffer, fence_fd) == 0 ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
}
