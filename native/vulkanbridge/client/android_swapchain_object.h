/* Internal object over images already created by a real backing provider. */
#ifndef RME_ANDROID_SWAPCHAIN_OBJECT_H
#define RME_ANDROID_SWAPCHAIN_OBJECT_H
#include "android_swapchain_flow.h"

struct RmeAndroidSwapchain;
struct RmeSwapchainBacking { struct ANativeWindowBuffer *buffer; VkImage image; };
typedef void (*RmeDestroyImage)(VkDevice, VkImage, const VkAllocationCallbacks *);
/* On success takes ownership of images and retains buffers; on failure takes neither. */
VkResult rme_swapchain_create_backed(VkDevice device, struct ANativeWindow *window,
    const struct RmeSwapchainBacking *backings, uint32_t count, const VkAllocationCallbacks *allocator,
    const struct RmeAndroidBufferRefs *refs, const struct RmeAndroidQueueOps *queue_ops,
    RmeDestroyImage destroy_image, struct RmeAndroidSwapchain **out);
VkResult rme_swapchain_images(const struct RmeAndroidSwapchain *, uint32_t *, VkImage *);
VkSwapchainKHR rme_swapchain_handle(const struct RmeAndroidSwapchain *);
void rme_swapchain_destroy_backed(struct RmeAndroidSwapchain *);
/* The actual Android-image provider is not connected: creation fails. */
VkResult rme_swapchain_create_vk(VkSwapchainKHR *out);
int rme_swapchain_ready(void);
int rme_swapchain_owns(VkSwapchainKHR handle);
int rme_swapchain_destroy_vk(VkSwapchainKHR handle);
int rme_swapchain_images_vk(VkSwapchainKHR handle, uint32_t *count, VkImage *images, VkResult *result);
int rme_swapchain_acquire_vk(VkSwapchainKHR handle, uint64_t timeout, VkSemaphore semaphore,
    VkFence fence, uint32_t *index, VkResult *result);
int rme_swapchain_present_vk(VkQueue queue, const VkPresentInfoKHR *present, VkResult *result);
#endif
