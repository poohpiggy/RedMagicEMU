#include "route.h"

int rme_route_surface_support(VkPhysicalDevice device, uint32_t queue_family,
    VkSurfaceKHR surface, VkBool32 *supported, VkResult *result,
    const struct RmeSurfaceSupportOps *ops) {
    void *window = 0;
    if (!ops || !ops->lookup_window || !ops->lookup_window(surface, &window, ops->context)) return 0;
    if (!window || !ops->query) {
        *supported = VK_FALSE;
        *result = VK_ERROR_SURFACE_LOST_KHR;
        return 1;
    }
    *result = ops->query(device, queue_family, window, supported, ops->context);
    return 1;
}
