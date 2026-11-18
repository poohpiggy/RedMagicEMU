#include "android_remote_swapchain.h"
#include "android_surface.h"
#include <pthread.h>
#include <stdlib.h>

struct RemoteSwapchain {
    VkInstance owner;
    VkSwapchainKHR handle;
    VkAllocationCallbacks allocator;
    int custom_allocator;
    struct RemoteSwapchain *next;
};
static pthread_mutex_t remote_lock = PTHREAD_MUTEX_INITIALIZER;
static struct RemoteSwapchain *remote_swapchains;

static void free_entry(struct RemoteSwapchain *entry) {
    if (entry->custom_allocator) entry->allocator.pfnFree(entry->allocator.pUserData, entry);
    else free(entry);
}

VkResult rme_remote_swapchain_create(VkInstance owner, VkDevice device,
    const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator, VkSwapchainKHR *out) {
    struct RemoteSwapchain *entry = allocator
        ? allocator->pfnAllocation(allocator->pUserData, sizeof(*entry),
                                  _Alignof(struct RemoteSwapchain), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)
        : malloc(sizeof(*entry));
    if (!entry) return VK_ERROR_OUT_OF_HOST_MEMORY;
    entry->custom_allocator = allocator != NULL;
    if (allocator) entry->allocator = *allocator;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkResult result = rme_android_surface_encoder_create_swapchain(device, info, allocator, &handle);
    if (result != VK_SUCCESS || handle == VK_NULL_HANDLE) {
        free_entry(entry);
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    }
    entry->owner = owner;
    entry->handle = handle;
    pthread_mutex_lock(&remote_lock);
    entry->next = remote_swapchains;
    remote_swapchains = entry;
    pthread_mutex_unlock(&remote_lock);
    *out = handle;
    return VK_SUCCESS;
}

int rme_remote_swapchain_owner(VkSwapchainKHR handle, VkInstance *owner) {
    pthread_mutex_lock(&remote_lock);
    struct RemoteSwapchain *entry = remote_swapchains;
    while (entry && entry->handle != handle) entry = entry->next;
    if (entry) *owner = entry->owner;
    pthread_mutex_unlock(&remote_lock);
    return entry != NULL;
}

int rme_remote_swapchain_forget(VkSwapchainKHR handle) {
    pthread_mutex_lock(&remote_lock);
    struct RemoteSwapchain **link = &remote_swapchains;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    struct RemoteSwapchain *entry = *link;
    if (entry) *link = entry->next;
    pthread_mutex_unlock(&remote_lock);
    if (!entry) return 0;
    free_entry(entry);
    return 1;
}
