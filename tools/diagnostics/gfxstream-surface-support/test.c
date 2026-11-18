#include "route.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

struct VkIcdSurfaceBase { int platform; };
struct wsi_device;
struct wsi_interface { VkResult (*get_support)(struct VkIcdSurfaceBase *, struct wsi_device *, uint32_t, VkBool32 *); };
struct wsi_device { struct wsi_interface *wsi[2]; };
struct vk_physical_device { struct wsi_device *wsi_device; };
#define VK_FROM_HANDLE(type, name, handle) struct type *name = (struct type *)(handle)
#define ICD_FROM_HANDLE(type, name, handle) struct type *name = (struct type *)(uintptr_t)(handle)
#include "mesa_support.inc"

/* An EMU surface begins with ANativeWindow*, not VkIcdSurfaceBase. */
struct EmuSurface { void *window; };
static struct EmuSurface local = {(void *)(uintptr_t)0x7fffffff};
static unsigned queried;
static int lookup(VkSurfaceKHR surface, void **window, void *ctx) {
    (void)ctx;
    if ((uintptr_t)surface != (uintptr_t)&local) return 0;
    *window = local.window;
    return 1;
}
static VkResult query(VkPhysicalDevice device, uint32_t queue_family, void *window, VkBool32 *supported, void *ctx) {
    assert(device && window == local.window && !ctx);
    queried++;
    if (queue_family == 4) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *supported = queue_family == 2 ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}
static VkResult mesa_fallback(struct VkIcdSurfaceBase *surface, struct wsi_device *wsi, uint32_t q, VkBool32 *supported) {
    assert(surface->platform == 1 && wsi && q == 2);
    *supported = VK_TRUE;
    return VK_SUCCESS;
}

extern int rme_route_surface_support(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *, VkResult *,
    const struct RmeSurfaceSupportOps *) __attribute__((weak));

static VkResult dispatch(VkPhysicalDevice device, uint32_t queue, VkSurfaceKHR surface, VkBool32 *out,
                         const struct RmeSurfaceSupportOps *ops) {
    VkResult result;
    if (rme_route_surface_support && rme_route_surface_support(device, queue, surface, out, &result, ops)) return result;
    return wsi_GetPhysicalDeviceSurfaceSupportKHR(device, queue, surface, out);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    struct wsi_interface iface = {mesa_fallback};
    struct wsi_device wsi = {{NULL, &iface}};
    struct vk_physical_device physical = {&wsi};
    VkPhysicalDevice device = (VkPhysicalDevice)&physical;
    struct VkIcdSurfaceBase foreign = {1};
    struct RmeSurfaceSupportOps ops = {lookup, query, NULL};
    VkSurfaceKHR surface = (VkSurfaceKHR)(uintptr_t)&local;
    VkBool32 supported = 55;
    if (!strcmp(argv[1], "foreign")) {
        assert(dispatch(device, 2, (VkSurfaceKHR)(uintptr_t)&foreign, &supported, &ops) == VK_SUCCESS && supported == VK_TRUE);
    } else if (!strcmp(argv[1], "missing")) {
        ops.query = NULL;
        assert(dispatch(device, 2, surface, &supported, &ops) == VK_ERROR_SURFACE_LOST_KHR && supported == VK_FALSE);
    } else if (!strcmp(argv[1], "provider_error")) {
        assert(dispatch(device, 4, surface, &supported, &ops) == VK_ERROR_OUT_OF_HOST_MEMORY && queried == 1);
    } else {
        uint32_t q = !strcmp(argv[1], "supported") ? 2 : 3;
        assert(dispatch(device, q, surface, &supported, &ops) == VK_SUCCESS);
        assert(supported == (q == 2 ? VK_TRUE : VK_FALSE) && queried == 1);
    }
    return 0;
}
