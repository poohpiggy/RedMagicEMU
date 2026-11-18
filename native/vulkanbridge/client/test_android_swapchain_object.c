/* CPU-only ownership test; no real buffers, images, fences or GPU. */
#include "android_swapchain_object.h"
#include <assert.h>
#include <stdint.h>

static int retained, released, destroyed;
static void retain(struct ANativeWindowBuffer *buffer) { assert(buffer); retained++; }
static void release(struct ANativeWindowBuffer *buffer) { assert(buffer); released++; }
static void destroy(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    assert(device && image && !allocator); destroyed++;
}
static const struct RmeAndroidBufferRefs refs = {retain, release};
static int releases, queues, cancels, dequeues;
static struct ANativeWindowBuffer *current_buffer;
static int set_timeout(struct ANativeWindow *window, int64_t timeout) { (void)window; (void)timeout; return 0; }
static int dequeue(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer, int *fd) {
    (void)window; dequeues++; *buffer = current_buffer; *fd = -1; return 0;
}
static int queue(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fd) {
    (void)window; (void)buffer; assert(fd == -1); queues++; return -1;
}
static int cancel(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fd) {
    (void)window; (void)buffer; assert(fd == -1); cancels++; return 0;
}
static int duplicate(int fd) { (void)fd; assert(0); return -1; }
static int close_fd(int fd) { (void)fd; assert(0); return 0; }
static VkResult acquire(VkDevice device, VkImage image, int fd, VkSemaphore semaphore, VkFence fence) {
    (void)device; (void)image; (void)semaphore; (void)fence; assert(fd == -1); return VK_SUCCESS;
}
static VkResult signal_release(VkQueue queue_handle, uint32_t count, const VkSemaphore *wait, VkImage image, int *fd) {
    (void)queue_handle; (void)count; (void)wait; (void)image; assert(*fd == -1); releases++; return VK_SUCCESS;
}
static const struct RmeAndroidQueueOps queue_ops = {set_timeout, dequeue, queue, cancel};
static const struct RmeAndroidDriverOps driver_ops = {duplicate, close_fd, acquire, signal_release};
const struct RmeAndroidDriverOps *rme_android_driver_ops(void) { return &driver_ops; }
int main(void) {
    VkDevice device = (VkDevice)(uintptr_t)1;
    struct ANativeWindow *window = (struct ANativeWindow *)(uintptr_t)2;
    struct RmeAndroidSwapchain *chain = NULL;
    struct RmeSwapchainBacking backings[2] = {
        {(struct ANativeWindowBuffer *)(uintptr_t)3, (VkImage)(uintptr_t)5},
        {(struct ANativeWindowBuffer *)(uintptr_t)4, (VkImage)(uintptr_t)6},
    };
    assert(rme_swapchain_create_backed(device, window, NULL, 0, NULL, &refs, &queue_ops, destroy, &chain) == VK_ERROR_INITIALIZATION_FAILED);
    assert(!chain && !retained && !destroyed);
    VkSwapchainKHR handle = (VkSwapchainKHR)(uintptr_t)1;
    assert(rme_swapchain_create_vk(&handle) == VK_ERROR_INITIALIZATION_FAILED && handle == VK_NULL_HANDLE);
    struct RmeSwapchainBacking duplicate[2] = {backings[0], backings[0]};
    assert(rme_swapchain_create_backed(device, window, duplicate, 2, NULL, &refs, &queue_ops, destroy, &chain) == VK_ERROR_INITIALIZATION_FAILED);
    assert(!chain && retained == 1 && released == 1 && !destroyed);
    assert(rme_swapchain_create_backed(device, window, backings, 2, NULL, &refs, &queue_ops, destroy, &chain) == VK_SUCCESS);
    assert(chain && retained == 3);
    uint32_t count = 0;
    assert(rme_swapchain_images(chain, &count, NULL) == VK_SUCCESS && count == 2);
    VkImage image = VK_NULL_HANDLE;
    count = 1;
    assert(rme_swapchain_images(chain, &count, &image) == VK_INCOMPLETE && count == 1 && image == backings[0].image);
    handle = rme_swapchain_handle(chain);
    VkResult result = VK_ERROR_DEVICE_LOST;
    count = 0;
    assert(rme_swapchain_images_vk(handle, &count, NULL, &result) && result == VK_SUCCESS && count == 2);
    assert(!rme_swapchain_images_vk((VkSwapchainKHR)(uintptr_t)1, &count, NULL, &result));
    current_buffer = backings[0].buffer;
    uint32_t index = 99;
    assert(rme_swapchain_acquire_vk(handle, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index, &result));
    assert(result == VK_SUCCESS && index == 0 && dequeues == 1);
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1, .pSwapchains = &handle, .pImageIndices = &index};
    assert(rme_swapchain_present_vk((VkQueue)(uintptr_t)7, &present, &result));
    assert(result == VK_ERROR_SURFACE_LOST_KHR && queues == 1 && releases == 1);
    assert(rme_swapchain_present_vk((VkQueue)(uintptr_t)7, &present, &result));
    assert(result == VK_ERROR_OUT_OF_DATE_KHR && queues == 1 && releases == 1);
    assert(rme_swapchain_acquire_vk(handle, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index, &result));
    assert(result == VK_ERROR_OUT_OF_DATE_KHR && dequeues == 1);
    assert(rme_swapchain_destroy_vk(handle));
    assert(!rme_swapchain_destroy_vk(handle));
    assert(released == 3 && destroyed == 2 && cancels == 1);
}
