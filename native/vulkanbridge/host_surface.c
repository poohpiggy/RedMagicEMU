/* EMU-owned Android window surface for the Guest's actual host VkInstance. */
#include "host_surface.h"
#include <stdlib.h>

struct RmeVkHostWindow { ANativeWindow* window; };
struct RmeVkHostSurface {
    VkInstance instance;
    VkSurfaceKHR surface;
    PFN_vkDestroySurfaceKHR destroy;
    ANativeWindow* window;
};

RmeVkHostWindow* rme_vk_host_window_acquire(ANativeWindow* window) {
    if (!window) return NULL;
    RmeVkHostWindow* owned = calloc(1, sizeof(*owned));
    if (!owned) return NULL;
    ANativeWindow_acquire(window);
    owned->window = window;
    return owned;
}

void rme_vk_host_window_release(RmeVkHostWindow* window) {
    if (!window) return;
    ANativeWindow_release(window->window);
    free(window);
}

VkResult rme_vk_host_surface_create(VkInstance instance, PFN_vkGetInstanceProcAddr get_proc,
                                    const RmeVkHostWindow* window, RmeVkHostSurface** out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    if (!instance || !get_proc || !window || !window->window) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkCreateAndroidSurfaceKHR create =
        (PFN_vkCreateAndroidSurfaceKHR)get_proc(instance, "vkCreateAndroidSurfaceKHR");
    PFN_vkDestroySurfaceKHR destroy =
        (PFN_vkDestroySurfaceKHR)get_proc(instance, "vkDestroySurfaceKHR");
    if (!create || !destroy) return VK_ERROR_EXTENSION_NOT_PRESENT;
    RmeVkHostSurface* owned = calloc(1, sizeof(*owned));
    if (!owned) return VK_ERROR_OUT_OF_HOST_MEMORY;
    ANativeWindow_acquire(window->window);
    VkAndroidSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = window->window,
    };
    VkResult result = create(instance, &info, NULL, &owned->surface);
    if (result != VK_SUCCESS || owned->surface == VK_NULL_HANDLE) {
        ANativeWindow_release(window->window);
        free(owned);
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    }
    owned->instance = instance;
    owned->destroy = destroy;
    owned->window = window->window;
    *out = owned;
    return VK_SUCCESS;
}

VkSurfaceKHR rme_vk_host_surface_handle(const RmeVkHostSurface* surface) {
    return surface ? surface->surface : VK_NULL_HANDLE;
}

void rme_vk_host_surface_destroy(RmeVkHostSurface* surface) {
    if (!surface) return;
    surface->destroy(surface->instance, surface->surface, NULL);
    ANativeWindow_release(surface->window);
    free(surface);
}
