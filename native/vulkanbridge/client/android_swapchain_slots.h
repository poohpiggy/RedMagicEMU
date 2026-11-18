/* Internal buffer-to-image identities. Does not allocate or share buffers. */
#ifndef RME_ANDROID_SWAPCHAIN_SLOTS_H
#define RME_ANDROID_SWAPCHAIN_SLOTS_H
#include <stdint.h>
#include <vulkan/vulkan_core.h>

struct ANativeWindowBuffer;
struct RmeAndroidBufferRefs {
    void (*retain)(struct ANativeWindowBuffer *);
    void (*release)(struct ANativeWindowBuffer *);
};
struct RmeSwapchainSlot {
    struct ANativeWindowBuffer *buffer;
    struct RmeAndroidBufferRefs refs;
    VkImage image;
    int dequeue_fence;
    int acquired;
};
VkResult rme_slot_register(struct RmeSwapchainSlot *slots, uint32_t count, uint32_t index,
    struct ANativeWindowBuffer *buffer, VkImage image, const struct RmeAndroidBufferRefs *refs);
VkResult rme_slot_unregister(struct RmeSwapchainSlot *slots, uint32_t count, uint32_t index);
const struct RmeAndroidBufferRefs *rme_android_buffer_refs(void);
/* On failure the caller retains ownership of the dequeued buffer and fence. */
VkResult rme_slot_claim(struct RmeSwapchainSlot *slots, uint32_t count,
    struct ANativeWindowBuffer *buffer, int fence, uint32_t *index);
/* Returns the stored dequeue fence to the caller without closing it. */
VkResult rme_slot_release(struct RmeSwapchainSlot *slots, uint32_t count,
    uint32_t index, int *fence);
#endif
