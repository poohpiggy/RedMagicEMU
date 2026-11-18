/* Host-local Vulkan Android surface ownership; no wire or Guest pointers. */
#ifndef RME_VULKANBRIDGE_HOST_SURFACE_H
#define RME_VULKANBRIDGE_HOST_SURFACE_H

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#error "host_surface requires VK_USE_PLATFORM_ANDROID_KHR"
#endif

typedef struct RmeVkHostWindow RmeVkHostWindow;
typedef struct RmeVkHostSurface RmeVkHostSurface;

#ifdef __cplusplus
extern "C" {
#endif

/* Only the trusted EMU host window owner calls this with its own local window.
 * The returned wrapper owns one reference; it must never be built from a Guest
 * address, an IPC payload, or a numeric handle received over the wire. */
RmeVkHostWindow* rme_vk_host_window_acquire(ANativeWindow* window);
void rme_vk_host_window_release(RmeVkHostWindow* window);

/* instance must be the actual unboxed host instance associated with the Guest
 * device, with both KHR_surface and KHR_android_surface enabled. get_proc must
 * be its Vulkan loader's vkGetInstanceProcAddr. The caller keeps the instance
 * alive until after rme_vk_host_surface_destroy, and externally synchronizes
 * all access to window and surface objects. No Guest address is accepted. */
VkResult rme_vk_host_surface_create(VkInstance instance, PFN_vkGetInstanceProcAddr get_proc,
                                    const RmeVkHostWindow* window, RmeVkHostSurface** out);
VkSurfaceKHR rme_vk_host_surface_handle(const RmeVkHostSurface* surface);
void rme_vk_host_surface_destroy(RmeVkHostSurface* surface);

#ifdef __cplusplus
}
#endif

#endif
