/* CPU-only dispatch test. It does not simulate or test a GPU backend. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "android_surface.h"
#include "android_swapchain_object.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_device.h"
#include "vk_object.h"
#include "gfxstream_vk_entrypoints.h"

const struct vk_instance_entrypoint_table gfxstream_vk_instance_entrypoints = {0};
static int instance_called, queue_called, device_called;
static int surface_allocs, surface_frees, surface_retains, surface_releases, surface_disconnects, fallback_destroy;
static int image_destroys, buffer_retains, buffer_releases;
static int surface_query_fallback;
static VKAPI_ATTR VkResult VKAPI_CALL surface_support_fallback(VkPhysicalDevice p, uint32_t q, VkSurfaceKHR s, VkBool32 *out)
{ (void)p; (void)q; (void)s; surface_query_fallback++; *out = VK_TRUE; return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL surface_caps_fallback(VkPhysicalDevice p, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *out)
{ (void)p; (void)s; (void)out; surface_query_fallback++; return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL surface_formats_fallback(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *count, VkSurfaceFormatKHR *out)
{ (void)p; (void)s; (void)count; (void)out; surface_query_fallback++; return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL surface_modes_fallback(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *count, VkPresentModeKHR *out)
{ (void)p; (void)s; (void)count; (void)out; surface_query_fallback++; return VK_SUCCESS; }
static void *test_alloc(void *user, size_t size, size_t alignment, VkSystemAllocationScope scope)
{ (void)alignment; (void)scope; (*(int *)user)++; return malloc(size); }
static void test_free(void *user, void *ptr) { (void)user; surface_frees++; free(ptr); }
static void retain_window(struct ANativeWindow *w) { assert(w); surface_retains++; }
static void release_window(struct ANativeWindow *w) { assert(w); surface_releases++; }
static int window_usage(struct ANativeWindow *w, uint64_t *usage) { assert(w); *usage = 1; return 0; }
static int window_connect(struct ANativeWindow *w) { assert(w); return 0; }
static int window_disconnect(struct ANativeWindow *w) { assert(w); surface_disconnects++; return 0; }
const struct RmeAndroidWindowOps *rme_android_window_ops(void) {
    static const struct RmeAndroidWindowOps ops = {retain_window, release_window, window_usage, window_connect, window_disconnect};
    return &ops;
}
#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE
static int wire_ready, remote_creates, remote_destroys, remote_support_queries, remote_swapchain_creates;
static int remote_swapchain_queries, remote_swapchain_destroys, remote_presents, remote_acquires;
static int remote_caps_queries, remote_formats_queries, remote_modes_queries;
static VkResult remote_swapchain_result = VK_SUCCESS;
static VkResult remote_acquire_result = VK_SUCCESS;
static VkSurfaceKHR remote_expected = (VkSurfaceKHR)(uintptr_t)0x1234;
static VkResult remote_create(VkInstance instance, VkSurfaceKHR *out) {
    assert(instance); ++remote_creates; *out = remote_expected; return VK_SUCCESS;
}
static void remote_destroy(VkInstance instance, VkSurfaceKHR surface) {
    assert(instance && surface == remote_expected); ++remote_destroys;
}
int rme_android_surface_wire_ready(VkInstance instance) { assert(instance); return wire_ready; }
VkResult rme_android_surface_create_encoded_vk(VkInstance instance, struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *out) {
    static const struct RmeRemoteSurfaceOps ops = {remote_create, remote_destroy};
    return rme_android_surface_create_remote_vk(instance, window, allocator, &ops, out);
}
VkResult rme_android_surface_encoder_support(VkPhysicalDevice physical, uint32_t queue,
    VkSurfaceKHR surface, VkBool32 *supported) {
    assert(physical && queue == 7 && surface == remote_expected);
    ++remote_support_queries; *supported = VK_TRUE; return VK_SUCCESS;
}
VkResult rme_android_surface_encoder_capabilities(VkPhysicalDevice physical, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *caps) {
    assert(physical && surface == remote_expected && caps); ++remote_caps_queries;
    caps->minImageCount = 2; return VK_SUCCESS;
}
VkResult rme_android_surface_encoder_formats(VkPhysicalDevice physical, VkSurfaceKHR surface,
    uint32_t *count, VkSurfaceFormatKHR *formats) {
    assert(physical && surface == remote_expected && count); ++remote_formats_queries;
    if (!formats) { *count = 2; return VK_SUCCESS; }
    assert(*count == 1); *count = 1; formats[0].format = VK_FORMAT_R8G8B8A8_UNORM; return VK_INCOMPLETE;
}
VkResult rme_android_surface_encoder_modes(VkPhysicalDevice physical, VkSurfaceKHR surface,
    uint32_t *count, VkPresentModeKHR *modes) {
    assert(physical && surface == remote_expected && count); ++remote_modes_queries;
    if (!modes) { *count = 2; return VK_SUCCESS; }
    assert(*count == 1); *count = 1; modes[0] = VK_PRESENT_MODE_FIFO_KHR; return VK_INCOMPLETE;
}
VkResult rme_android_surface_encoder_create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *out) {
    assert(device && info && info->surface == remote_expected && !allocator);
    assert(info->imageExtent.width == 321 && info->pNext == (void *)(uintptr_t)5);
    ++remote_swapchain_creates;
    if (remote_swapchain_result == VK_SUCCESS) *out = (VkSwapchainKHR)(uintptr_t)0x8888;
    return remote_swapchain_result;
}
static VKAPI_ATTR VkResult VKAPI_CALL fallback_swapchain_images(VkDevice device, VkSwapchainKHR swapchain,
    uint32_t *count, VkImage *images) {
    (void)device; (void)swapchain; (void)count; (void)images; return VK_ERROR_INITIALIZATION_FAILED;
}
VkResult rme_android_surface_encoder_swapchain_images(VkDevice device, VkSwapchainKHR swapchain,
    uint32_t *count, VkImage *images) {
    assert(device && (uintptr_t)swapchain == 0x8888 && count);
    ++remote_swapchain_queries;
    if (!images) { *count = 1; return VK_SUCCESS; }
    assert(*count >= 1); *count = 1; images[0] = (VkImage)(uintptr_t)0x9999; return VK_INCOMPLETE;
}
void rme_android_surface_encoder_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *allocator) {
    assert(device && (uintptr_t)swapchain == 0x8888 && !allocator); ++remote_swapchain_destroys;
}
static VKAPI_ATTR VkResult VKAPI_CALL fallback_present(VkQueue queue, const VkPresentInfoKHR *info) {
    (void)queue; (void)info; return VK_ERROR_INITIALIZATION_FAILED;
}
VkResult rme_android_surface_encoder_present(VkQueue queue, const VkPresentInfoKHR *info) {
    assert(queue && info && info->swapchainCount == 1 && (uintptr_t)info->pSwapchains[0] == 0x8888);
    assert(info->pImageIndices[0] == 0 && info->pResults);
    ++remote_presents; info->pResults[0] = VK_SUCCESS; return VK_SUBOPTIMAL_KHR;
}
static VKAPI_ATTR VkResult VKAPI_CALL fallback_acquire(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index) {
    (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)fence; (void)index;
    return VK_ERROR_INITIALIZATION_FAILED;
}
VkResult rme_android_surface_encoder_acquire(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index) {
    assert(device && (uintptr_t)swapchain == 0x8888 && timeout == 456);
    assert((uintptr_t)semaphore == 4 && (uintptr_t)fence == 5 && index && *index == 0);
    ++remote_acquires; *index = 3; return remote_acquire_result;
}
#endif
static VKAPI_ATTR void VKAPI_CALL destroy_other_surface(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks *a)
{ (void)i; (void)a; assert((uintptr_t)s == 1); fallback_destroy++; }
static void retain_buffer(struct ANativeWindowBuffer *b) { assert(b); buffer_retains++; }
static void release_buffer(struct ANativeWindowBuffer *b) { assert(b); buffer_releases++; }
static void destroy_image(VkDevice d, VkImage i, const VkAllocationCallbacks *a)
{ assert(d && i && !a); image_destroys++; }
#ifdef RME_TEST_PROMOTED
static int map_called;
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice, const VkMemoryMapInfoKHR *, void **);
static VKAPI_ATTR VkResult VKAPI_CALL
map_memory(VkDevice device, const VkMemoryMapInfoKHR *info, void **data)
{ (void)device; (void)info; *data = (void *)0x1234; map_called++; return VK_SUCCESS; }
#endif

VKAPI_ATTR VkResult VKAPI_CALL gfxstream_vk_EnumerateInstanceVersion(uint32_t *p)
{ *p = 7; return VK_SUCCESS; }

static VKAPI_ATTR VkResult VKAPI_CALL
enumerate(VkInstance instance, uint32_t *count, VkPhysicalDevice *devices)
{ (void)instance; (void)devices; instance_called++; *count = 3; return VK_SUCCESS; }

static VKAPI_ATTR VkResult VKAPI_CALL
submit(VkQueue queue, uint32_t count, const VkSubmitInfo *infos, VkFence fence)
{ (void)queue; (void)infos; (void)fence; queue_called++; return count == 2 ? VK_SUCCESS : VK_ERROR_UNKNOWN; }

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
device_lookup(VkDevice device, const char *name)
{
    (void)device; device_called++;
    if (!strcmp(name, "vkQueueSubmit")) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (!strcmp(name, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)vkQueueSubmit;
    return NULL;
}

PFN_vkVoidFunction vk_instance_get_proc_addr(const struct vk_instance *instance,
    const struct vk_instance_entrypoint_table *entries, const char *name)
{
    (void)entries;
    assert(!instance);
    return name && !strcmp(name, "vkEnumerateInstanceVersion")
        ? (PFN_vkVoidFunction)gfxstream_vk_EnumerateInstanceVersion : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
gfxstream_vk_GetInstanceProcAddr(VkInstance instance, const char *name)
{
    assert(instance);
    if (name && !strncmp(name, "vkGetPhysicalDeviceSurface", 26)) {
        struct vk_instance *i = (struct vk_instance *)instance;
        return i->enabled_extensions.KHR_surface ? (PFN_vkVoidFunction)gfxstream_vk_EnumerateInstanceVersion : NULL;
    }
    if (name && (!strcmp(name, "vkGetDeviceProcAddr") || !strcmp(name, "vkDestroySurfaceKHR") ||
                 !strcmp(name, "vkCreateSwapchainKHR") || !strcmp(name, "vkCreateAndroidSurfaceKHR")))
        return (PFN_vkVoidFunction)gfxstream_vk_EnumerateInstanceVersion;
    return name && !strcmp(name, "vkEnumeratePhysicalDevices")
        ? (PFN_vkVoidFunction)vkEnumeratePhysicalDevices : NULL;
}

int main(void)
{
    /* Vulkan explicitly allows destroying a null instance or device. */
    vkDestroyInstance(VK_NULL_HANDLE, NULL);
    vkDestroyDevice(VK_NULL_HANDLE, NULL);
    struct vk_instance instance = {0};
    struct vk_device device = {0};
    struct vk_physical_device physical = {0};
    struct vk_object_base queue = {0};
    uint32_t value = 0;
    instance.base.type = VK_OBJECT_TYPE_INSTANCE;
    device.base.type = VK_OBJECT_TYPE_DEVICE;
    physical.base.type = VK_OBJECT_TYPE_PHYSICAL_DEVICE;
    physical.dispatch_table.GetPhysicalDeviceSurfaceSupportKHR = surface_support_fallback;
    physical.dispatch_table.GetPhysicalDeviceSurfaceCapabilitiesKHR = surface_caps_fallback;
    physical.dispatch_table.GetPhysicalDeviceSurfaceFormatsKHR = surface_formats_fallback;
    physical.dispatch_table.GetPhysicalDeviceSurfacePresentModesKHR = surface_modes_fallback;
    instance.alloc.pUserData = &surface_allocs;
    instance.alloc.pfnAllocation = test_alloc;
    instance.alloc.pfnFree = test_free;
    instance.dispatch_table.DestroySurfaceKHR = destroy_other_surface;
    instance.dispatch_table.EnumeratePhysicalDevices = enumerate;
    device.dispatch_table.QueueSubmit = submit;
    device.dispatch_table.GetDeviceProcAddr = device_lookup;
#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE
    device.dispatch_table.GetSwapchainImagesKHR = fallback_swapchain_images;
    device.dispatch_table.QueuePresentKHR = fallback_present;
    device.dispatch_table.AcquireNextImageKHR = fallback_acquire;
#endif
    queue.device = &device;
#ifdef RME_TEST_PROMOTED
    device.dispatch_table.MapMemory2KHR = map_memory;
    void *mapped = NULL;
    assert(vkMapMemory2((VkDevice)&device, NULL, &mapped) == VK_SUCCESS && mapped == (void *)0x1234 && map_called == 1);
#endif
    assert(vkEnumerateInstanceVersion(&value) == VK_SUCCESS && value == 7);
    assert(vkGetInstanceProcAddr(NULL, "vkGetInstanceProcAddr") == (PFN_vkVoidFunction)vkGetInstanceProcAddr);
    assert(vkGetInstanceProcAddr(NULL, "vkDoesNotExist") == NULL);
    assert(vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion") == (PFN_vkVoidFunction)gfxstream_vk_EnumerateInstanceVersion);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkDoesNotExist") == NULL);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkCreateAndroidSurfaceKHR") == NULL);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkCreateSwapchainKHR") == NULL);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkGetDeviceProcAddr") == (PFN_vkVoidFunction)vkGetDeviceProcAddr);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkDestroySurfaceKHR") == (PFN_vkVoidFunction)vkDestroySurfaceKHR);
    instance.enabled_extensions.KHR_android_surface = true;
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkCreateAndroidSurfaceKHR") == (PFN_vkVoidFunction)vkCreateAndroidSurfaceKHR);
    instance.enabled_extensions.KHR_android_surface = false;
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkEnumeratePhysicalDevices") == (PFN_vkVoidFunction)vkEnumeratePhysicalDevices);
    assert(vkEnumeratePhysicalDevices((VkInstance)&instance, &value, NULL) == VK_SUCCESS && value == 3 && instance_called == 1);
    assert(vkGetDeviceProcAddr((VkDevice)&device, "vkDoesNotExist") == NULL && device_called == 1);
    assert(vkGetDeviceProcAddr((VkDevice)&device, "vkQueueSubmit") == (PFN_vkVoidFunction)vkQueueSubmit && device_called == 2);
    device.enabled_extensions.KHR_swapchain = true;
    assert(vkGetDeviceProcAddr((VkDevice)&device, "vkCreateSwapchainKHR") == NULL);
    assert(vkQueueSubmit((VkQueue)&queue, 2, NULL, VK_NULL_HANDLE) == VK_SUCCESS && queue_called == 1);
    VkAndroidSurfaceCreateInfoKHR create = {0};
    create.window = (struct ANativeWindow *)(uintptr_t)1;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    assert(vkCreateAndroidSurfaceKHR((VkInstance)&instance, &create, NULL, &surface) == VK_SUCCESS);
    assert(surface && surface_allocs == 1 && surface_retains == 1);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkGetPhysicalDeviceSurfaceSupportKHR") == NULL);
    instance.enabled_extensions.KHR_surface = true;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_support = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)
        vkGetInstanceProcAddr((VkInstance)&instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    assert(get_support == vkGetPhysicalDeviceSurfaceSupportKHR);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkGetPhysicalDeviceSurfaceFormatsKHR") == (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR);
    assert(vkGetInstanceProcAddr((VkInstance)&instance, "vkGetPhysicalDeviceSurfacePresentModesKHR") == (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR);
    VkBool32 supported = VK_TRUE;
    VkSurfaceCapabilitiesKHR caps = {0};
    uint32_t count = 0;
    assert(vkGetPhysicalDeviceSurfaceSupportKHR((VkPhysicalDevice)&physical, 0, surface, &supported) == VK_ERROR_SURFACE_LOST_KHR && supported == VK_FALSE);
    supported = VK_TRUE;
    assert(get_support((VkPhysicalDevice)&physical, 0, surface, &supported) == VK_ERROR_SURFACE_LOST_KHR && supported == VK_FALSE);
    assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR((VkPhysicalDevice)&physical, surface, &caps) == VK_ERROR_SURFACE_LOST_KHR);
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR((VkPhysicalDevice)&physical, surface, &count, NULL) == VK_ERROR_SURFACE_LOST_KHR);
    assert(vkGetPhysicalDeviceSurfacePresentModesKHR((VkPhysicalDevice)&physical, surface, &count, NULL) == VK_ERROR_SURFACE_LOST_KHR);
    assert(surface_query_fallback == 0);
    VkSurfaceKHR other_surface = (VkSurfaceKHR)(uintptr_t)2;
    assert(vkGetPhysicalDeviceSurfaceSupportKHR((VkPhysicalDevice)&physical, 0, other_surface, &supported) == VK_SUCCESS && supported == VK_TRUE);
    assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR((VkPhysicalDevice)&physical, other_surface, &caps) == VK_SUCCESS);
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR((VkPhysicalDevice)&physical, other_surface, &count, NULL) == VK_SUCCESS);
    assert(vkGetPhysicalDeviceSurfacePresentModesKHR((VkPhysicalDevice)&physical, other_surface, &count, NULL) == VK_SUCCESS);
    assert(surface_query_fallback == 4);
    vkDestroySurfaceKHR((VkInstance)&instance, surface, NULL);
    assert(surface_frees == 1 && surface_releases == 1 && surface_disconnects == 1);
    int explicit_allocs = 0;
    VkAllocationCallbacks explicit_allocator = {0};
    explicit_allocator.pUserData = &explicit_allocs;
    explicit_allocator.pfnAllocation = test_alloc;
    explicit_allocator.pfnFree = test_free;
    assert(vkCreateAndroidSurfaceKHR((VkInstance)&instance, &create, &explicit_allocator, &surface) == VK_SUCCESS);
    assert(explicit_allocs == 1 && surface_allocs == 1);
    vkDestroySurfaceKHR((VkInstance)&instance, surface, &explicit_allocator);
    assert(surface_frees == 2 && surface_releases == 2 && surface_disconnects == 2);
#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE
    wire_ready = 1;
    assert(vkCreateAndroidSurfaceKHR((VkInstance)&instance, &create, NULL, &surface) == VK_SUCCESS);
    VkSurfaceKHR remote = VK_NULL_HANDLE;
    assert(remote_creates == 1 && rme_android_surface_remote_vk(surface, &remote) && remote == remote_expected);
    VkBool32 remote_supported = VK_FALSE;
    assert(vkGetPhysicalDeviceSurfaceSupportKHR((VkPhysicalDevice)&physical, 7, surface, &remote_supported) == VK_SUCCESS);
    assert(remote_supported == VK_TRUE && remote_support_queries == 1);
    VkSurfaceCapabilitiesKHR remote_caps = {0};
    assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR((VkPhysicalDevice)&physical, surface, &remote_caps) == VK_SUCCESS);
    assert(remote_caps.minImageCount == 2 && remote_caps_queries == 1);
    uint32_t format_count = 0;
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR((VkPhysicalDevice)&physical, surface, &format_count, NULL) == VK_SUCCESS);
    assert(format_count == 2);
    VkSurfaceFormatKHR format = {0}; format_count = 1;
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR((VkPhysicalDevice)&physical, surface, &format_count, &format) == VK_INCOMPLETE);
    assert(format_count == 1 && format.format == VK_FORMAT_R8G8B8A8_UNORM && remote_formats_queries == 2);
    uint32_t mode_count = 0;
    assert(vkGetPhysicalDeviceSurfacePresentModesKHR((VkPhysicalDevice)&physical, surface, &mode_count, NULL) == VK_SUCCESS);
    assert(mode_count == 2);
    VkPresentModeKHR mode = VK_PRESENT_MODE_IMMEDIATE_KHR; mode_count = 1;
    assert(vkGetPhysicalDeviceSurfacePresentModesKHR((VkPhysicalDevice)&physical, surface, &mode_count, &mode) == VK_INCOMPLETE);
    assert(mode_count == 1 && mode == VK_PRESENT_MODE_FIFO_KHR && remote_modes_queries == 2);
    VkSwapchainCreateInfoKHR remote_info = {0};
    remote_info.surface = surface;
    remote_info.imageExtent.width = 321;
    remote_info.pNext = (void *)(uintptr_t)5;
    VkSwapchainKHR remote_chain = (VkSwapchainKHR)(uintptr_t)0x7777;
    remote_swapchain_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(vkCreateSwapchainKHR((VkDevice)&device, &remote_info, NULL, &remote_chain) == VK_ERROR_OUT_OF_HOST_MEMORY);
    assert((uintptr_t)remote_chain == 0x7777 && remote_swapchain_creates == 1);
    assert(remote_info.surface == surface && remote_info.pNext == (void *)(uintptr_t)5);
    remote_swapchain_result = VK_SUCCESS;
    assert(vkCreateSwapchainKHR((VkDevice)&device, &remote_info, NULL, &remote_chain) == VK_SUCCESS);
    assert((uintptr_t)remote_chain == 0x8888 && remote_swapchain_creates == 2 && remote_info.surface == surface);
    uint32_t remote_count = 0;
    assert(vkGetSwapchainImagesKHR((VkDevice)&device, remote_chain, &remote_count, NULL) == VK_SUCCESS);
    assert(remote_count == 1 && remote_swapchain_queries == 1);
    VkImage remote_image = VK_NULL_HANDLE;
    assert(vkGetSwapchainImagesKHR((VkDevice)&device, remote_chain, &remote_count, &remote_image) == VK_INCOMPLETE);
    assert((uintptr_t)remote_image == 0x9999 && remote_swapchain_queries == 2);
    uint32_t acquired_index = 77;
    remote_acquire_result = VK_TIMEOUT;
    assert(vkAcquireNextImageKHR((VkDevice)&device, remote_chain, 456,
        (VkSemaphore)(uintptr_t)4, (VkFence)(uintptr_t)5, &acquired_index) == VK_TIMEOUT);
    assert(acquired_index == 77 && remote_acquires == 1);
    remote_acquire_result = VK_SUBOPTIMAL_KHR;
    assert(vkAcquireNextImageKHR((VkDevice)&device, remote_chain, 456,
        (VkSemaphore)(uintptr_t)4, (VkFence)(uintptr_t)5, &acquired_index) == VK_SUBOPTIMAL_KHR);
    assert(acquired_index == 3 && remote_acquires == 2);
    uint32_t remote_index = 0;
    VkResult per_swapchain = VK_ERROR_UNKNOWN;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .swapchainCount = 1,
        .pSwapchains = &remote_chain, .pImageIndices = &remote_index, .pResults = &per_swapchain};
    assert(vkQueuePresentKHR((VkQueue)&queue, &present) == VK_SUBOPTIMAL_KHR);
    assert(remote_presents == 1 && per_swapchain == VK_SUCCESS);
    wire_ready = 0;
    assert(vkGetPhysicalDeviceSurfaceSupportKHR((VkPhysicalDevice)&physical, 7, surface, &remote_supported) == VK_ERROR_SURFACE_LOST_KHR);
    assert(remote_supported == VK_FALSE && remote_support_queries == 1);
    format_count = 77;
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR((VkPhysicalDevice)&physical, surface, &format_count, NULL) == VK_ERROR_SURFACE_LOST_KHR);
    assert(format_count == 77 && remote_formats_queries == 2);
    assert(vkGetSwapchainImagesKHR((VkDevice)&device, remote_chain, &remote_count, NULL) == VK_ERROR_SURFACE_LOST_KHR);
    assert(remote_swapchain_queries == 2);
    acquired_index = 77;
    assert(vkAcquireNextImageKHR((VkDevice)&device, remote_chain, 456,
        (VkSemaphore)(uintptr_t)4, (VkFence)(uintptr_t)5, &acquired_index) == VK_ERROR_SURFACE_LOST_KHR);
    assert(acquired_index == 77 && remote_acquires == 2);
    assert(vkQueuePresentKHR((VkQueue)&queue, &present) == VK_ERROR_SURFACE_LOST_KHR);
    assert(remote_presents == 1);
    VkSwapchainKHR unchanged_chain = (VkSwapchainKHR)(uintptr_t)0x7777;
    assert(vkCreateSwapchainKHR((VkDevice)&device, &remote_info, NULL, &unchanged_chain) == VK_ERROR_SURFACE_LOST_KHR);
    assert((uintptr_t)unchanged_chain == 0x7777 && remote_swapchain_creates == 2);
    vkDestroySwapchainKHR((VkDevice)&device, remote_chain, NULL);
    assert(remote_swapchain_destroys == 0);
    wire_ready = 1;
    vkDestroySwapchainKHR((VkDevice)&device, remote_chain, NULL);
    assert(remote_swapchain_destroys == 1);
    wire_ready = 0;
    vkDestroySurfaceKHR((VkInstance)&instance, surface, NULL);
    assert(remote_destroys == 0 && rme_android_surface_remote_vk(surface, &remote));
    wire_ready = 1;
    vkDestroySurfaceKHR((VkInstance)&instance, surface, NULL);
    assert(remote_destroys == 1);
#endif
    vkDestroySurfaceKHR((VkInstance)&instance, (VkSurfaceKHR)(uintptr_t)1, NULL);
    assert(fallback_destroy == 1);
    VkSwapchainKHR swapchain = (VkSwapchainKHR)(uintptr_t)1;
    VkSwapchainCreateInfoKHR swap_info = {0};
    assert(vkCreateSwapchainKHR((VkDevice)&device, &swap_info, NULL, &swapchain) == VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain == VK_NULL_HANDLE);
    const struct RmeAndroidBufferRefs refs = {retain_buffer, release_buffer};
    const struct RmeAndroidQueueOps queue_ops = {0};
    struct RmeSwapchainBacking backing = {(struct ANativeWindowBuffer *)(uintptr_t)4, (VkImage)(uintptr_t)5};
    struct RmeAndroidSwapchain *chain;
    assert(rme_swapchain_create_backed((VkDevice)&device, (struct ANativeWindow *)(uintptr_t)1,
        &backing, 1, NULL, &refs, &queue_ops, destroy_image, &chain) == VK_SUCCESS);
    swapchain = rme_swapchain_handle(chain);
    VkAcquireNextImageInfoKHR acquire2 = {0};
    acquire2.swapchain = swapchain;
    acquire2.deviceMask = 2;
    assert(vkAcquireNextImage2KHR((VkDevice)&device, &acquire2, &value) == VK_ERROR_FEATURE_NOT_PRESENT);
    value = 0;
    assert(vkGetSwapchainImagesKHR((VkDevice)&device, swapchain, &value, NULL) == VK_SUCCESS && value == 1);
    vkDestroySwapchainKHR((VkDevice)&device, swapchain, NULL);
    assert(buffer_retains == 1 && buffer_releases == 1 && image_destroys == 1);
    return 0;
}
