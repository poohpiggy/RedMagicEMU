#pragma once
#include <vulkan/vulkan_core.h>
namespace gfxstream { namespace vk {
class VkEncoder {
public:
    bool rmeWsiEnabled() const { return rme_wsi_enabled; }
    void rmeSetWsiEnabled(bool value) { rme_wsi_enabled = value; }
    VkResult vkCreateEmuSurfaceRME(VkInstance, VkSurfaceKHR*, uint32_t);
    void vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*, uint32_t);
    VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*, uint32_t);
    VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*, uint32_t);
    VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*, uint32_t);
    VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*, uint32_t);
    VkResult vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*, uint32_t);
    VkResult vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*, uint32_t);
    void vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*, uint32_t);
    VkResult vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*, uint32_t);
    VkResult vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*, uint32_t);
private:
    bool rme_wsi_enabled = false;
};
}}
