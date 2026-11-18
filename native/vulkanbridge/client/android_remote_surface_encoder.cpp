/* Paired gfxstream encoder for the isolated remote-surface experiment. */
#include "android_surface.h"
#include "HostConnection.h"
#include "VkEncoder.h"

static gfxstream::vk::VkEncoder *surface_encoder() {
    HostConnection *connection = HostConnection::getOrCreate(kCapsetGfxStreamVulkan);
    return connection ? connection->vkEncoder() : nullptr;
}

#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE
extern "C" int rme_android_surface_wire_ready(VkInstance instance) {
    if (!instance) return 0;
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    return encoder && encoder->rmeWsiEnabled();
}
#endif

static VkResult create_surface(VkInstance instance, VkSurfaceKHR *surface) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkCreateEmuSurfaceRME(instance, surface, 1);
}

static void destroy_surface(VkInstance instance, VkSurfaceKHR surface) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (encoder) encoder->vkDestroySurfaceKHR(instance, surface, nullptr, 1);
}

extern "C" const struct RmeRemoteSurfaceOps *rme_android_surface_encoder_ops(void) {
    static const struct RmeRemoteSurfaceOps ops = {create_surface, destroy_surface};
    return &ops;
}

extern "C" VkResult rme_android_surface_create_encoded_vk(VkInstance instance, ANativeWindow *window,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface) {
    return rme_android_surface_create_remote_vk(instance, window, allocator,
                                                rme_android_surface_encoder_ops(), surface);
}

extern "C" VkResult rme_android_surface_encoder_support(VkPhysicalDevice physical, uint32_t queue,
    VkSurfaceKHR remote, VkBool32 *supported) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkGetPhysicalDeviceSurfaceSupportKHR(physical, queue, remote, supported, 1);
}

extern "C" VkResult rme_android_surface_encoder_capabilities(VkPhysicalDevice physical, VkSurfaceKHR remote,
    VkSurfaceCapabilitiesKHR *capabilities) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, remote, capabilities, 1);
}

extern "C" VkResult rme_android_surface_encoder_formats(VkPhysicalDevice physical, VkSurfaceKHR remote,
    uint32_t *count, VkSurfaceFormatKHR *formats) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkGetPhysicalDeviceSurfaceFormatsKHR(physical, remote, count, formats, 1);
}

extern "C" VkResult rme_android_surface_encoder_modes(VkPhysicalDevice physical, VkSurfaceKHR remote,
    uint32_t *count, VkPresentModeKHR *modes) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkGetPhysicalDeviceSurfacePresentModesKHR(physical, remote, count, modes, 1);
}

extern "C" VkResult rme_android_surface_encoder_create_swapchain(VkDevice device,
    const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkCreateSwapchainKHR(device, info, allocator, swapchain, 1);
}

extern "C" VkResult rme_android_surface_encoder_swapchain_images(VkDevice device, VkSwapchainKHR swapchain,
    uint32_t *count, VkImage *images) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkGetSwapchainImagesKHR(device, swapchain, count, images, 1);
}

extern "C" void rme_android_surface_encoder_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *allocator) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (encoder) encoder->vkDestroySwapchainKHR(device, swapchain, allocator, 1);
}

extern "C" VkResult rme_android_surface_encoder_present(VkQueue queue, const VkPresentInfoKHR *info) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkQueuePresentKHR(queue, info, 1);
}

extern "C" VkResult rme_android_surface_encoder_acquire(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index) {
    gfxstream::vk::VkEncoder *encoder = surface_encoder();
    if (!encoder) return VK_ERROR_DEVICE_LOST;
    return encoder->vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, index, 1);
}
