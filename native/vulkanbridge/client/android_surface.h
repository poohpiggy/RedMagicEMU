/* EMU-internal Android window lifecycle; no Vulkan extension is advertised. */
#ifndef RME_ANDROID_SURFACE_H
#define RME_ANDROID_SURFACE_H
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

struct ANativeWindow;
struct RmeAndroidSurface;
struct RmeAndroidWindowOps {
    void (*retain)(struct ANativeWindow *);
    void (*release)(struct ANativeWindow *);
    int (*consumer_usage)(struct ANativeWindow *, uint64_t *);
    int (*connect)(struct ANativeWindow *);
    int (*disconnect)(struct ANativeWindow *);
};
/* The remote handle is a Guest-side gfxstream handle, never a host pointer. */
struct RmeRemoteSurfaceOps {
    VkResult (*create)(VkInstance, VkSurfaceKHR *);
    void (*destroy)(VkInstance, VkSurfaceKHR);
};

#ifdef __cplusplus
extern "C" {
#endif
VkResult rme_android_surface_create(struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, const struct RmeAndroidWindowOps *ops,
    struct RmeAndroidSurface **out);
void rme_android_surface_destroy(struct RmeAndroidSurface *surface);
struct ANativeWindow *rme_android_surface_window(const struct RmeAndroidSurface *surface);
uint64_t rme_android_surface_usage(const struct RmeAndroidSurface *surface);
const struct RmeAndroidWindowOps *rme_android_window_ops(void);
VkResult rme_android_surface_create_vk(struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *out);
int rme_android_surface_destroy_vk(VkSurfaceKHR surface);
int rme_android_surface_owned_vk(VkSurfaceKHR surface);
VkResult rme_android_surface_create_remote_vk(VkInstance instance, struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, const struct RmeRemoteSurfaceOps *ops, VkSurfaceKHR *out);
int rme_android_surface_remote_vk(VkSurfaceKHR surface, VkSurfaceKHR *remote);
int rme_android_surface_remote_info_vk(VkSurfaceKHR surface, VkInstance *instance, VkSurfaceKHR *remote);
/* Provided only by the paired experimental gfxstream encoder build. */
const struct RmeRemoteSurfaceOps *rme_android_surface_encoder_ops(void);
VkResult rme_android_surface_create_encoded_vk(VkInstance instance, struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *out);
VkResult rme_android_surface_encoder_support(VkPhysicalDevice physical, uint32_t queue,
    VkSurfaceKHR remote, VkBool32 *supported);
VkResult rme_android_surface_encoder_capabilities(VkPhysicalDevice physical, VkSurfaceKHR remote,
    VkSurfaceCapabilitiesKHR *capabilities);
VkResult rme_android_surface_encoder_formats(VkPhysicalDevice physical, VkSurfaceKHR remote,
    uint32_t *count, VkSurfaceFormatKHR *formats);
VkResult rme_android_surface_encoder_modes(VkPhysicalDevice physical, VkSurfaceKHR remote,
    uint32_t *count, VkPresentModeKHR *modes);
VkResult rme_android_surface_encoder_create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain);
VkResult rme_android_surface_encoder_swapchain_images(VkDevice device, VkSwapchainKHR swapchain,
    uint32_t *count, VkImage *images);
void rme_android_surface_encoder_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *allocator);
VkResult rme_android_surface_encoder_present(VkQueue queue, const VkPresentInfoKHR *info);
VkResult rme_android_surface_encoder_acquire(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *index);
#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE
/* Must be supplied by the same-channel protocol negotiation, not Guest input. */
int rme_android_surface_wire_ready(VkInstance instance);
#endif
#ifdef __cplusplus
}
#endif
#endif
