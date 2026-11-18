/* CPU-only identity and ownership tests; synthetic buffers and fds only. */
#include "android_swapchain_slots.h"
#include <assert.h>
#include <stdint.h>

static int retains, releases;
static void retain(struct ANativeWindowBuffer *buffer) { assert(buffer); retains++; }
static void release(struct ANativeWindowBuffer *buffer) { assert(buffer); releases++; }

int main(void) {
    struct RmeSwapchainSlot slots[2] = {0};
    struct ANativeWindowBuffer *a = (struct ANativeWindowBuffer *)(uintptr_t)1;
    struct ANativeWindowBuffer *b = (struct ANativeWindowBuffer *)(uintptr_t)2;
    struct ANativeWindowBuffer *unknown = (struct ANativeWindowBuffer *)(uintptr_t)3;
    VkImage ia = (VkImage)(uintptr_t)4;
    VkImage ib = (VkImage)(uintptr_t)5;
    uint32_t index = 99;
    int fd = -2;
    const struct RmeAndroidBufferRefs refs = {retain, release};
    assert(rme_slot_register(slots, 2, 0, a, ia, &refs) == VK_SUCCESS);
    assert(rme_slot_register(slots, 2, 1, b, ib, &refs) == VK_SUCCESS);
    assert(retains == 2);
    assert(rme_slot_claim(slots, 2, unknown, 10, &index) == VK_ERROR_OUT_OF_DATE_KHR);
    assert(rme_slot_claim(slots, 2, b, 11, &index) == VK_SUCCESS && index == 1);
    assert(rme_slot_claim(slots, 2, b, 12, &index) == VK_ERROR_OUT_OF_DATE_KHR);
    assert(rme_slot_release(slots, 2, 1, &fd) == VK_SUCCESS && fd == 11);
    assert(rme_slot_release(slots, 2, 1, &fd) == VK_ERROR_OUT_OF_DATE_KHR);
    assert(rme_slot_claim(slots, 2, a, -1, &index) == VK_SUCCESS && index == 0);
    assert(rme_slot_unregister(slots, 2, 0) == VK_ERROR_OUT_OF_DATE_KHR);
    assert(rme_slot_release(slots, 2, 0, &fd) == VK_SUCCESS && fd == -1);
    assert(rme_slot_unregister(slots, 2, 0) == VK_SUCCESS);
    assert(rme_slot_unregister(slots, 2, 1) == VK_SUCCESS);
    assert(releases == 2);
}
