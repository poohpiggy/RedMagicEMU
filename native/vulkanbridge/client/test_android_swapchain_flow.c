/* CPU-only ownership and error tests; no real fd, queue, driver or GPU. */
#include "android_swapchain_flow.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>

static struct ANativeWindowBuffer *buffer = (struct ANativeWindowBuffer *)(uintptr_t)2;
static int dup_calls, acquire_calls, driver_consumed_fds, release_calls, cancel_calls, queue_calls, closed_fd;
static int native_queue_rc, dup_failure, release_fd = -1;
static VkResult acquire_result = VK_SUCCESS, release_result = VK_SUCCESS;
static int set_timeout(struct ANativeWindow *w, int64_t t) { assert(w); (void)t; return 0; }
static int dequeue(struct ANativeWindow *w, struct ANativeWindowBuffer **b, int *fd) { assert(w); *b = buffer; *fd = 17; return 0; }
static int queue(struct ANativeWindow *w, struct ANativeWindowBuffer *b, int fd) { assert(w && b == buffer); queue_calls++; assert(fd == release_fd); return native_queue_rc; }
static int cancel(struct ANativeWindow *w, struct ANativeWindowBuffer *b, int fd) { assert(w && b == buffer && fd == 17); cancel_calls++; return 0; }
static int duplicate(int fd) { assert(fd == 17); dup_calls++; return dup_failure ? -1 : 117; }
static int close_fd(int fd) { closed_fd = fd; return 0; }
static VkResult acquire(VkDevice d, VkImage image, int fd, VkSemaphore s, VkFence f) {
    assert(d && image && fd == 117 && !s && !f); acquire_calls++; driver_consumed_fds++; return acquire_result;
}
static VkResult release(VkQueue q, uint32_t count, const VkSemaphore *wait, VkImage image, int *fd) {
    assert(q && !count && !wait && image && *fd == -1); release_calls++; *fd = release_fd; return release_result;
}
static void retain_buffer(struct ANativeWindowBuffer *b) { assert(b == buffer); }
static void release_buffer(struct ANativeWindowBuffer *b) { assert(b == buffer); }
static const struct RmeAndroidQueueOps queue_ops = {set_timeout, dequeue, queue, cancel};
static const struct RmeAndroidDriverOps driver_ops = {duplicate, close_fd, acquire, release};
static const struct RmeAndroidBufferRefs buffer_refs = {retain_buffer, release_buffer};
int main(void) {
    struct ANativeWindow *window = (struct ANativeWindow *)(uintptr_t)1;
    struct RmeSwapchainSlot slots[1] = {0};
    VkDevice device = (VkDevice)(uintptr_t)3;
    VkQueue vkqueue = (VkQueue)(uintptr_t)4;
    VkImage image = (VkImage)(uintptr_t)5;
    uint32_t index = 99;
    assert(rme_swapchain_acquire(window, &queue_ops, &driver_ops, slots, 1, device, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index) == VK_ERROR_OUT_OF_DATE_KHR);
    assert(cancel_calls == 1 && dup_calls == 0);
    assert(rme_slot_register(slots, 1, 0, buffer, image, &buffer_refs) == VK_SUCCESS);
    dup_failure = 1;
    assert(rme_swapchain_acquire(window, &queue_ops, &driver_ops, slots, 1, device, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index) == VK_ERROR_DEVICE_LOST);
    assert(cancel_calls == 2 && !slots[0].acquired && acquire_calls == 0);
    dup_failure = 0; acquire_result = VK_ERROR_DEVICE_LOST;
    assert(rme_swapchain_acquire(window, &queue_ops, &driver_ops, slots, 1, device, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index) == VK_ERROR_DEVICE_LOST);
    assert(cancel_calls == 3 && !slots[0].acquired && acquire_calls == 1 && driver_consumed_fds == 1);
    acquire_result = VK_SUCCESS;
    assert(rme_swapchain_acquire(window, &queue_ops, &driver_ops, slots, 1, device, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, &index) == VK_SUCCESS);
    assert(index == 0 && slots[0].acquired && slots[0].dequeue_fence == 17 && driver_consumed_fds == 2);
    release_result = VK_ERROR_DEVICE_LOST; release_fd = 88; closed_fd = -2;
    assert(rme_swapchain_present(window, &queue_ops, &driver_ops, slots, 1, vkqueue, 0, 0, NULL) == VK_ERROR_DEVICE_LOST);
    assert(release_calls == 1 && queue_calls == 0 && closed_fd == 88 && slots[0].acquired);
    release_result = VK_SUCCESS; native_queue_rc = -EIO; closed_fd = -2;
    assert(rme_swapchain_present(window, &queue_ops, &driver_ops, slots, 1, vkqueue, 0, 0, NULL) == VK_ERROR_SURFACE_LOST_KHR);
    assert(queue_calls == 1 && closed_fd == -2 && slots[0].acquired && slots[0].dequeue_fence == 17);
    native_queue_rc = 0;
    assert(rme_swapchain_present(window, &queue_ops, &driver_ops, slots, 1, vkqueue, 0, 0, NULL) == VK_SUCCESS);
    assert(queue_calls == 2 && closed_fd == 17 && !slots[0].acquired);
}
