#include "android_swapchain_object.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

struct RmeAndroidSwapchain {
    VkDevice device;
    struct ANativeWindow *window;
    struct RmeAndroidQueueOps queue_ops;
    VkAllocationCallbacks allocator;
    RmeDestroyImage destroy_image;
    uint32_t count;
    int custom_allocator;
    int retired;
    struct RmeAndroidSwapchain *next;
    struct RmeSwapchainSlot slots[];
};
static pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;
static struct RmeAndroidSwapchain *chains;

static struct RmeAndroidSwapchain *find_chain(VkSwapchainKHR handle) {
    struct RmeAndroidSwapchain *target = (struct RmeAndroidSwapchain *)(uintptr_t)handle;
    struct RmeAndroidSwapchain *chain = chains;
    while (chain && chain != target) chain = chain->next;
    return chain;
}

static void free_chain(struct RmeAndroidSwapchain *chain) {
    if (chain->custom_allocator) chain->allocator.pfnFree(chain->allocator.pUserData, chain);
    else free(chain);
}

VkResult rme_swapchain_create_backed(VkDevice device, struct ANativeWindow *window,
    const struct RmeSwapchainBacking *backings, uint32_t count, const VkAllocationCallbacks *allocator,
    const struct RmeAndroidBufferRefs *refs, const struct RmeAndroidQueueOps *queue_ops,
    RmeDestroyImage destroy_image, struct RmeAndroidSwapchain **out) {
    *out = NULL;
    if (!backings || count == 0) return VK_ERROR_INITIALIZATION_FAILED;
    size_t slot_bytes, size;
    if (__builtin_mul_overflow((size_t)count, sizeof(struct RmeSwapchainSlot), &slot_bytes) ||
        __builtin_add_overflow(sizeof(struct RmeAndroidSwapchain), slot_bytes, &size))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    struct RmeAndroidSwapchain *chain = allocator
        ? allocator->pfnAllocation(allocator->pUserData, size, _Alignof(struct RmeAndroidSwapchain), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)
        : malloc(size);
    if (!chain) return VK_ERROR_OUT_OF_HOST_MEMORY;
    chain->device = device;
    chain->window = window;
    chain->queue_ops = *queue_ops;
    chain->destroy_image = destroy_image;
    chain->custom_allocator = allocator != NULL;
    chain->retired = 0;
    if (allocator) chain->allocator = *allocator;
    chain->count = 0;
    for (uint32_t i = 0; i < count; i++) {
        chain->slots[i] = (struct RmeSwapchainSlot){0};
        VkResult result = rme_slot_register(chain->slots, i + 1, i, backings[i].buffer, backings[i].image, refs);
        if (result != VK_SUCCESS) {
            for (uint32_t j = 0; j < i; j++) (void)rme_slot_unregister(chain->slots, i, j);
            free_chain(chain);
            return result;
        }
        chain->count++;
    }
    pthread_mutex_lock(&chain_lock);
    chain->next = chains;
    chains = chain;
    pthread_mutex_unlock(&chain_lock);
    *out = chain;
    return VK_SUCCESS;
}

VkResult rme_swapchain_images(const struct RmeAndroidSwapchain *chain, uint32_t *count, VkImage *images) {
    if (!images) { *count = chain->count; return VK_SUCCESS; }
    uint32_t requested = *count;
    uint32_t written = requested < chain->count ? requested : chain->count;
    for (uint32_t i = 0; i < written; i++) images[i] = chain->slots[i].image;
    *count = written;
    return written < chain->count ? VK_INCOMPLETE : VK_SUCCESS;
}

void rme_swapchain_destroy_backed(struct RmeAndroidSwapchain *chain) {
    if (!chain) return;
    pthread_mutex_lock(&chain_lock);
    struct RmeAndroidSwapchain **link = &chains;
    while (*link && *link != chain) link = &(*link)->next;
    if (*link) *link = chain->next;
    pthread_mutex_unlock(&chain_lock);
    for (uint32_t i = 0; i < chain->count; i++) {
        if (chain->slots[i].acquired) {
            int fence;
            if (rme_slot_release(chain->slots, chain->count, i, &fence) == VK_SUCCESS)
                (void)rme_android_cancel(chain->window, &chain->queue_ops, chain->slots[i].buffer, fence);
        }
        VkImage image = chain->slots[i].image;
        (void)rme_slot_unregister(chain->slots, chain->count, i);
        chain->destroy_image(chain->device, image, NULL);
    }
    free_chain(chain);
}

VkSwapchainKHR rme_swapchain_handle(const struct RmeAndroidSwapchain *chain) {
    return (VkSwapchainKHR)(uintptr_t)chain;
}

VkResult rme_swapchain_create_vk(VkSwapchainKHR *out) {
    *out = VK_NULL_HANDLE;
    /* No verified native-buffer/host-image provider exists yet. */
    return VK_ERROR_INITIALIZATION_FAILED;
}

int rme_swapchain_ready(void) {
    /* The real native-buffer to host-image provider is not implemented. */
    return 0;
}

int rme_swapchain_owns(VkSwapchainKHR handle) {
    pthread_mutex_lock(&chain_lock);
    int owned = find_chain(handle) != NULL;
    pthread_mutex_unlock(&chain_lock);
    return owned;
}

int rme_swapchain_destroy_vk(VkSwapchainKHR handle) {
    pthread_mutex_lock(&chain_lock);
    struct RmeAndroidSwapchain *chain = find_chain(handle);
    pthread_mutex_unlock(&chain_lock);
    if (!chain) return 0;
    rme_swapchain_destroy_backed(chain);
    return 1;
}

int rme_swapchain_images_vk(VkSwapchainKHR handle, uint32_t *count, VkImage *images, VkResult *result) {
    pthread_mutex_lock(&chain_lock);
    struct RmeAndroidSwapchain *chain = find_chain(handle);
    if (chain) *result = rme_swapchain_images(chain, count, images);
    pthread_mutex_unlock(&chain_lock);
    return chain != NULL;
}

int rme_swapchain_acquire_vk(VkSwapchainKHR handle, uint64_t timeout, VkSemaphore semaphore,
    VkFence fence, uint32_t *index, VkResult *result) {
    pthread_mutex_lock(&chain_lock);
    struct RmeAndroidSwapchain *chain = find_chain(handle);
    pthread_mutex_unlock(&chain_lock);
    if (!chain) return 0;
    if (chain->retired) {
        *result = VK_ERROR_OUT_OF_DATE_KHR;
        return 1;
    }
    *result = rme_swapchain_acquire(chain->window, &chain->queue_ops, rme_android_driver_ops(),
        chain->slots, chain->count, chain->device, timeout, semaphore, fence, index);
    return 1;
}

int rme_swapchain_present_vk(VkQueue queue, const VkPresentInfoKHR *present, VkResult *result) {
    pthread_mutex_lock(&chain_lock);
    struct RmeAndroidSwapchain *chain = NULL;
    for (uint32_t i = 0; i < present->swapchainCount; i++) {
        chain = find_chain(present->pSwapchains[i]);
        if (chain) break;
    }
    pthread_mutex_unlock(&chain_lock);
    if (!chain) return 0;
    if (chain->retired || present->swapchainCount != 1 || present->pNext) {
        *result = VK_ERROR_OUT_OF_DATE_KHR;
    } else {
        *result = rme_swapchain_present(chain->window, &chain->queue_ops, rme_android_driver_ops(),
            chain->slots, chain->count, queue, present->pImageIndices[0],
            present->waitSemaphoreCount, present->pWaitSemaphores);
        if (*result != VK_SUCCESS) chain->retired = 1;
    }
    if (present->pResults && present->swapchainCount == 1) present->pResults[0] = *result;
    return 1;
}
