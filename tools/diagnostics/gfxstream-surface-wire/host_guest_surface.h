/* Local test candidate: never accept a Guest window pointer. */
#pragma once
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
VkResult rme_vk_guest_surface_create(VkInstance, PFN_vkGetInstanceProcAddr, VkSurfaceKHR*);
int rme_vk_guest_surface_destroy(VkInstance, VkSurfaceKHR);
#ifdef __cplusplus
}
#endif
