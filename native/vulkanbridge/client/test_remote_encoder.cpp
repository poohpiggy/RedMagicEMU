/* CPU-only: exercise the real adapter with substitute encoder/connection. */
#include "android_surface.h"
#include "test_remote_encoder/HostConnection.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>

static HostConnection connection;
static thread_local gfxstream::vk::VkEncoder encoder;
static bool have_connection = true, have_encoder = true;
static unsigned creates, destroys, queries, swapchain_creates, swapchain_queries, swapchain_destroys;
static unsigned presents, acquires, caps_queries, format_queries, mode_queries;
static unsigned retains, releases, connects, disconnects;
static VkInstance expected_instance = (VkInstance)(uintptr_t)1;
static VkSurfaceKHR expected_surface = (VkSurfaceKHR)(uintptr_t)2;
static void retain(ANativeWindow* window) { assert(window); ++retains; }
static void release(ANativeWindow* window) { assert(window); ++releases; }
static int usage(ANativeWindow* window, uint64_t* out) { assert(window); *out = 1; return 0; }
static int connect_window(ANativeWindow* window) { assert(window); ++connects; return 0; }
static int disconnect_window(ANativeWindow* window) { assert(window); ++disconnects; return 0; }
extern "C" const RmeAndroidWindowOps* rme_android_window_ops() {
    static const RmeAndroidWindowOps ops = {retain, release, usage, connect_window, disconnect_window};
    return &ops;
}
HostConnection* HostConnection::getOrCreate(int capset) {
    assert(capset == kCapsetGfxStreamVulkan);
    return have_connection ? &connection : nullptr;
}
gfxstream::vk::VkEncoder* HostConnection::vkEncoder() { return have_encoder ? &encoder : nullptr; }
VkResult gfxstream::vk::VkEncoder::vkCreateEmuSurfaceRME(VkInstance instance, VkSurfaceKHR* surface, uint32_t lock) {
    assert(instance == expected_instance && lock == 1); ++creates; *surface = expected_surface; return VK_SUCCESS;
}
void gfxstream::vk::VkEncoder::vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
    const VkAllocationCallbacks* allocator, uint32_t lock) {
    assert(instance == expected_instance && surface == expected_surface && !allocator && lock == 1); ++destroys;
}
VkResult gfxstream::vk::VkEncoder::vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physical,
    uint32_t queue, VkSurfaceKHR surface, VkBool32* supported, uint32_t lock) {
    assert(physical && queue == 7 && surface == expected_surface && lock == 1);
    ++queries; *supported = VK_TRUE; return VK_SUCCESS;
}
VkResult gfxstream::vk::VkEncoder::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physical,
    VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* caps, uint32_t lock) {
    assert(physical && surface == expected_surface && caps && lock == 1);
    ++caps_queries; caps->minImageCount = 2; return VK_SUCCESS;
}
VkResult gfxstream::vk::VkEncoder::vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physical,
    VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats, uint32_t lock) {
    assert(physical && surface == expected_surface && count && lock == 1); ++format_queries;
    if (!formats) { *count = 2; return VK_SUCCESS; }
    assert(*count == 1); formats[0].format = VK_FORMAT_R8G8B8A8_UNORM; *count = 1; return VK_INCOMPLETE;
}
VkResult gfxstream::vk::VkEncoder::vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physical,
    VkSurfaceKHR surface, uint32_t* count, VkPresentModeKHR* modes, uint32_t lock) {
    assert(physical && surface == expected_surface && count && lock == 1); ++mode_queries;
    if (!modes) { *count = 2; return VK_SUCCESS; }
    assert(*count == 1); modes[0] = VK_PRESENT_MODE_FIFO_KHR; *count = 1; return VK_INCOMPLETE;
}
VkResult gfxstream::vk::VkEncoder::vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* out, uint32_t lock) {
    assert(device && info && info->surface == expected_surface && !allocator && lock == 1);
    ++swapchain_creates; *out = (VkSwapchainKHR)(uintptr_t)8; return VK_SUCCESS;
}
VkResult gfxstream::vk::VkEncoder::vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR chain,
    uint32_t* count, VkImage* images, uint32_t lock) {
    assert(device && (uintptr_t)chain == 8 && count && !images && lock == 1);
    ++swapchain_queries; *count = 1; return VK_SUCCESS;
}
void gfxstream::vk::VkEncoder::vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR chain,
    const VkAllocationCallbacks* allocator, uint32_t lock) {
    assert(device && (uintptr_t)chain == 8 && !allocator && lock == 1); ++swapchain_destroys;
}
VkResult gfxstream::vk::VkEncoder::vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info, uint32_t lock) {
    assert(queue && info && info->swapchainCount == 1 && lock == 1);
    assert(info->pResults); *info->pResults = VK_SUBOPTIMAL_KHR; ++presents;
    return VK_SUBOPTIMAL_KHR;
}
VkResult gfxstream::vk::VkEncoder::vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR chain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t* index, uint32_t lock) {
    assert((uintptr_t)device == 4 && (uintptr_t)chain == 8 && timeout == 123);
    assert((uintptr_t)semaphore == 6 && (uintptr_t)fence == 7 && index && lock == 1);
    *index = 2; ++acquires; return VK_SUBOPTIMAL_KHR;
}
extern "C" VkResult rme_android_surface_encoder_present(VkQueue, const VkPresentInfoKHR*);
extern "C" VkResult rme_android_surface_encoder_acquire(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
static void* verify_other_channel(void*) {
    assert(!rme_android_surface_wire_ready(expected_instance));
    encoder.rmeSetWsiEnabled(true);
    assert(rme_android_surface_wire_ready(expected_instance));
    return nullptr;
}
int main() {
    assert(!rme_android_surface_wire_ready(VK_NULL_HANDLE));
    have_connection = false;
    assert(!rme_android_surface_wire_ready(expected_instance));
    have_connection = true; have_encoder = false;
    assert(!rme_android_surface_wire_ready(expected_instance));
    have_encoder = true;
    assert(!rme_android_surface_wire_ready(expected_instance));
    encoder.rmeSetWsiEnabled(true);
    assert(rme_android_surface_wire_ready(expected_instance));
    pthread_t thread;
    assert(pthread_create(&thread, nullptr, verify_other_channel, nullptr) == 0);
    assert(pthread_join(thread, nullptr) == 0);
    assert(rme_android_surface_wire_ready(expected_instance));
    const RmeRemoteSurfaceOps* ops = rme_android_surface_encoder_ops();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    have_connection = false;
    assert(ops->create(expected_instance, &surface) == VK_ERROR_DEVICE_LOST && !creates);
    VkSwapchainCreateInfoKHR no_connection_info = {};
    no_connection_info.surface = expected_surface;
    VkSwapchainKHR no_connection_swapchain = (VkSwapchainKHR)(uintptr_t)9;
    assert(rme_android_surface_encoder_create_swapchain((VkDevice)(uintptr_t)4,
        &no_connection_info, nullptr, &no_connection_swapchain) == VK_ERROR_DEVICE_LOST);
    VkPresentInfoKHR no_connection_present = {};
    assert(rme_android_surface_encoder_present((VkQueue)(uintptr_t)5, &no_connection_present) == VK_ERROR_DEVICE_LOST);
    uint32_t no_connection_index = 9;
    assert(rme_android_surface_encoder_acquire((VkDevice)(uintptr_t)4, (VkSwapchainKHR)(uintptr_t)8, 123,
        (VkSemaphore)(uintptr_t)6, (VkFence)(uintptr_t)7, &no_connection_index) == VK_ERROR_DEVICE_LOST);
    assert(no_connection_index == 9 && acquires == 0);
    uint32_t no_connection_count = 9;
    assert(rme_android_surface_encoder_formats((VkPhysicalDevice)(uintptr_t)4, expected_surface,
        &no_connection_count, nullptr) == VK_ERROR_DEVICE_LOST);
    assert(no_connection_count == 9 && format_queries == 0);
    assert((uintptr_t)no_connection_swapchain == 9 && !swapchain_creates);
    have_connection = true; have_encoder = false;
    assert(ops->create(expected_instance, &surface) == VK_ERROR_DEVICE_LOST && !creates);
    have_encoder = true;
    assert(ops->create(expected_instance, &surface) == VK_SUCCESS && surface == expected_surface && creates == 1);
    ops->destroy(expected_instance, surface);
    assert(destroys == 1);
    ANativeWindow* window = (ANativeWindow*)(uintptr_t)3;
    VkSurfaceKHR local = VK_NULL_HANDLE;
    have_connection = false;
    assert(rme_android_surface_create_encoded_vk(expected_instance, window, nullptr, &local) == VK_ERROR_DEVICE_LOST);
    assert(local == VK_NULL_HANDLE && retains == 1 && releases == 1 && connects == 1 && disconnects == 1);
    have_connection = true;
    assert(rme_android_surface_create_encoded_vk(expected_instance, window, nullptr, &local) == VK_SUCCESS);
    VkSurfaceKHR remote = VK_NULL_HANDLE;
    assert(rme_android_surface_remote_vk(local, &remote) && remote == expected_surface && local != remote);
    VkBool32 supported = VK_FALSE;
    assert(rme_android_surface_encoder_support((VkPhysicalDevice)(uintptr_t)4, 7, remote, &supported) == VK_SUCCESS);
    assert(queries == 1 && supported == VK_TRUE);
    VkSurfaceCapabilitiesKHR caps = {};
    assert(rme_android_surface_encoder_capabilities((VkPhysicalDevice)(uintptr_t)4, remote, &caps) == VK_SUCCESS);
    assert(caps.minImageCount == 2 && caps_queries == 1);
    uint32_t format_count = 0;
    assert(rme_android_surface_encoder_formats((VkPhysicalDevice)(uintptr_t)4, remote, &format_count, nullptr) == VK_SUCCESS && format_count == 2);
    VkSurfaceFormatKHR format = {}; format_count = 1;
    assert(rme_android_surface_encoder_formats((VkPhysicalDevice)(uintptr_t)4, remote, &format_count, &format) == VK_INCOMPLETE);
    assert(format_count == 1 && format.format == VK_FORMAT_R8G8B8A8_UNORM && format_queries == 2);
    uint32_t mode_count = 0;
    assert(rme_android_surface_encoder_modes((VkPhysicalDevice)(uintptr_t)4, remote, &mode_count, nullptr) == VK_SUCCESS && mode_count == 2);
    VkPresentModeKHR mode = VK_PRESENT_MODE_IMMEDIATE_KHR; mode_count = 1;
    assert(rme_android_surface_encoder_modes((VkPhysicalDevice)(uintptr_t)4, remote, &mode_count, &mode) == VK_INCOMPLETE);
    assert(mode_count == 1 && mode == VK_PRESENT_MODE_FIFO_KHR && mode_queries == 2);
    VkSwapchainCreateInfoKHR info = {};
    info.surface = remote;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    assert(rme_android_surface_encoder_create_swapchain((VkDevice)(uintptr_t)4, &info, nullptr, &swapchain) == VK_SUCCESS);
    assert(swapchain_creates == 1 && (uintptr_t)swapchain == 8);
    uint32_t acquired_index = 99;
    assert(rme_android_surface_encoder_acquire((VkDevice)(uintptr_t)4, swapchain, 123,
        (VkSemaphore)(uintptr_t)6, (VkFence)(uintptr_t)7, &acquired_index) == VK_SUBOPTIMAL_KHR);
    assert(acquired_index == 2 && acquires == 1);
    VkResult per_result = VK_SUCCESS;
    uint32_t image_index = 0;
    VkPresentInfoKHR present = {};
    present.swapchainCount = 1; present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index; present.pResults = &per_result;
    assert(rme_android_surface_encoder_present((VkQueue)(uintptr_t)5, &present) == VK_SUBOPTIMAL_KHR);
    assert(presents == 1 && per_result == VK_SUBOPTIMAL_KHR);
    uint32_t count = 0;
    assert(rme_android_surface_encoder_swapchain_images((VkDevice)(uintptr_t)4, swapchain, &count, nullptr) == VK_SUCCESS);
    assert(count == 1 && swapchain_queries == 1);
    rme_android_surface_encoder_destroy_swapchain((VkDevice)(uintptr_t)4, swapchain, nullptr);
    assert(swapchain_destroys == 1);
    assert(rme_android_surface_destroy_vk(local));
    assert(creates == 2 && destroys == 2 && retains == 2 && releases == 2 && connects == 2 && disconnects == 2);
}
