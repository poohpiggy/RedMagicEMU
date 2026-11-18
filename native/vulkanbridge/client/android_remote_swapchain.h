/* Experimental registry for swapchains created by the paired gfxstream encoder. */
#ifndef RME_ANDROID_REMOTE_SWAPCHAIN_H
#define RME_ANDROID_REMOTE_SWAPCHAIN_H
#include <vulkan/vulkan_core.h>
#ifdef __cplusplus
extern "C" {
#endif
VkResult rme_remote_swapchain_create(VkInstance owner, VkDevice device,
    const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator, VkSwapchainKHR *out);
int rme_remote_swapchain_owner(VkSwapchainKHR swapchain, VkInstance *owner);
int rme_remote_swapchain_forget(VkSwapchainKHR swapchain);
#ifdef __cplusplus
}
#endif
#endif
