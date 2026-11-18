/* Real device/queue dispatcher calls; this does not create shared images. */
#include "android_native_driver.h"
#include "vk_device.h"
#include "vk_object.h"
#include <unistd.h>

VkResult rme_android_driver_acquire(VkDevice device, VkImage image, int fence_fd,
    VkSemaphore semaphore, VkFence fence) {
    VK_FROM_HANDLE(vk_device, vk_device, device);
    if (!vk_device->dispatch_table.AcquireImageANDROID) {
        if (fence_fd >= 0) close(fence_fd);
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return vk_device->dispatch_table.AcquireImageANDROID(device, image, fence_fd, semaphore, fence);
}

VkResult rme_android_driver_release(VkQueue queue, uint32_t wait_count,
    const VkSemaphore *wait_semaphores, VkImage image, int *release_fence_fd) {
    struct vk_object_base *base = (struct vk_object_base *)queue;
    if (!base->device->dispatch_table.QueueSignalReleaseImageANDROID) {
        if (release_fence_fd) *release_fence_fd = -1;
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return base->device->dispatch_table.QueueSignalReleaseImageANDROID(
        queue, wait_count, wait_semaphores, image, release_fence_fd);
}
