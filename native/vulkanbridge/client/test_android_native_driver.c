/* CPU-only dispatch test; no real fence descriptors, window or GPU used. */
#include "android_native_driver.h"
#include "vk_device.h"
#include "vk_object.h"
#include <assert.h>

static int acquired, released;
static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice d, VkImage image, int fd, VkSemaphore semaphore, VkFence fence) {
    assert(d && image && fd == -1 && !semaphore && !fence); acquired++; return VK_ERROR_DEVICE_LOST;
}
static VKAPI_ATTR VkResult VKAPI_CALL release(VkQueue q, uint32_t count, const VkSemaphore *wait, VkImage image, int *fd) {
    assert(q && count == 0 && !wait && image); released++; *fd = -1; return VK_ERROR_DEVICE_LOST;
}
int main(void) {
    struct vk_device dev = {0};
    struct vk_object_base queue = {0};
    dev.base.type = VK_OBJECT_TYPE_DEVICE;
    queue.device = &dev;
    VkImage image = (VkImage)(uintptr_t)1;
    int fence_fd = 42;
    assert(rme_android_driver_acquire((VkDevice)&dev, image, -1, VK_NULL_HANDLE, VK_NULL_HANDLE) == VK_ERROR_EXTENSION_NOT_PRESENT);
    assert(rme_android_driver_release((VkQueue)&queue, 0, NULL, image, &fence_fd) == VK_ERROR_EXTENSION_NOT_PRESENT);
    assert(fence_fd == -1 && acquired == 0 && released == 0);
    dev.dispatch_table.AcquireImageANDROID = acquire;
    dev.dispatch_table.QueueSignalReleaseImageANDROID = release;
    assert(rme_android_driver_acquire((VkDevice)&dev, image, -1, VK_NULL_HANDLE, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST);
    assert(rme_android_driver_release((VkQueue)&queue, 0, NULL, image, &fence_fd) == VK_ERROR_DEVICE_LOST);
    assert(acquired == 1 && released == 1 && fence_fd == -1);
}
