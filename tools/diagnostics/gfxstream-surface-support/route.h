#pragma once
#include <vulkan/vulkan.h>

/* Both callbacks are supplied by the owner of the surface registry. */
struct RmeSurfaceSupportOps {
    int (*lookup_window)(VkSurfaceKHR, void **window, void *context);
    VkResult (*query)(VkPhysicalDevice, uint32_t, void *window, VkBool32 *supported, void *context);
    void *context;
};

/* Return 1 if this implementation handled the surface; 0 means fall through. */
int rme_route_surface_support(VkPhysicalDevice device, uint32_t queue_family,
    VkSurfaceKHR surface, VkBool32 *supported, VkResult *result,
    const struct RmeSurfaceSupportOps *ops);
