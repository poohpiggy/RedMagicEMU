#ifndef RME_ANDROID_READBACK_WSI_H
#define RME_ANDROID_READBACK_WSI_H
#include <vulkan/vulkan.h>

VkResult rme_readback_support(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *);
VkResult rme_readback_capabilities(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *);
VkResult rme_readback_formats(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *);
VkResult rme_readback_modes(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkPresentModeKHR *);
VkResult rme_readback_create(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
int rme_readback_destroy(VkSwapchainKHR);
int rme_readback_images(VkSwapchainKHR, uint32_t *, VkImage *, VkResult *);
int rme_readback_acquire(VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *, VkResult *);
int rme_readback_present(VkQueue, const VkPresentInfoKHR *, VkResult *);
#endif
