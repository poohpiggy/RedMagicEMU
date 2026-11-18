#include "android_swapchain_slots.h"
#include <stddef.h>

VkResult rme_slot_register(struct RmeSwapchainSlot *slots, uint32_t count, uint32_t index,
    struct ANativeWindowBuffer *buffer, VkImage image, const struct RmeAndroidBufferRefs *refs) {
    if (index >= count || !buffer || image == VK_NULL_HANDLE || slots[index].buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < count; i++)
        if (slots[i].buffer == buffer) return VK_ERROR_INITIALIZATION_FAILED;
    slots[index].buffer = buffer;
    slots[index].image = image;
    slots[index].refs = *refs;
    slots[index].refs.retain(buffer);
    slots[index].dequeue_fence = -1;
    slots[index].acquired = 0;
    return VK_SUCCESS;
}

VkResult rme_slot_unregister(struct RmeSwapchainSlot *slots, uint32_t count, uint32_t index) {
    if (index >= count || !slots[index].buffer || slots[index].acquired) return VK_ERROR_OUT_OF_DATE_KHR;
    slots[index].refs.release(slots[index].buffer);
    slots[index].buffer = NULL;
    slots[index].image = VK_NULL_HANDLE;
    return VK_SUCCESS;
}

VkResult rme_slot_claim(struct RmeSwapchainSlot *slots, uint32_t count,
    struct ANativeWindowBuffer *buffer, int fence, uint32_t *index) {
    for (uint32_t i = 0; i < count; i++) {
        if (slots[i].buffer != buffer) continue;
        if (slots[i].acquired) return VK_ERROR_OUT_OF_DATE_KHR;
        slots[i].acquired = 1;
        slots[i].dequeue_fence = fence;
        *index = i;
        return VK_SUCCESS;
    }
    return VK_ERROR_OUT_OF_DATE_KHR;
}

VkResult rme_slot_release(struct RmeSwapchainSlot *slots, uint32_t count,
    uint32_t index, int *fence) {
    if (index >= count || !slots[index].acquired) return VK_ERROR_OUT_OF_DATE_KHR;
    *fence = slots[index].dequeue_fence;
    slots[index].dequeue_fence = -1;
    slots[index].acquired = 0;
    return VK_SUCCESS;
}
