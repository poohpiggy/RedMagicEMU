// CPU-only test of the actual extracted gfxstream extension-discovery functions.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#define VK_SUCCESS 0
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_DEVICE_EXTENSION_COUNT 2
#define VK_INSTANCE_EXTENSION_COUNT 3
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME "VK_KHR_swapchain"
#define VK_KHR_ANDROID_SURFACE_EXTENSION_NAME "VK_KHR_android_surface"
using VkResult = int;
using VkPhysicalDevice = void*;
struct VkExtensionProperties { char extensionName[256]; uint32_t specVersion; };
struct vk_device_extension_table { bool extensions[2] = {}; };
struct vk_instance_extension_table { bool extensions[3] = {}; };
static VkExtensionProperties vk_device_extensions[2] = {{"VK_KHR_swapchain", 1}, {"VK_EXT_other", 1}};
static VkExtensionProperties vk_instance_extensions[3] = {{"VK_KHR_surface", 1}, {"VK_KHR_android_surface", 1}, {"VK_EXT_other", 1}};
static bool instance_extension_table_initialized;
static vk_instance_extension_table gfxstream_vk_instance_extensions_supported;
static bool isMesaOnlyDeviceExtension(const char *name) { return !strcmp(name, "VK_KHR_swapchain"); }
static bool isMesaOnlyInstanceExtension(const char *name) { return !strcmp(name, "VK_KHR_surface"); }
static VkResult SetupInstanceForProcess() { return VK_SUCCESS; }
#define VK_HOST_CONNECTION(ret) auto vkEnc = (void*)1;
namespace gfxstream { namespace vk {
struct ResourceTracker {
    static void *getThreadLocalEncoder() { return (void*)1; }
    static ResourceTracker *get() { static ResourceTracker instance; return &instance; }
    VkResult on_vkEnumerateDeviceExtensionProperties(void*, int, void*, void*, uint32_t *count, VkExtensionProperties *out) {
        if (!out) { *count = 2; return 0; } out[0] = vk_device_extensions[0]; out[1] = vk_device_extensions[1]; return 0;
    }
    VkResult on_vkEnumerateInstanceExtensionProperties(void*, int, void*, uint32_t *count, VkExtensionProperties *out) {
        if (!out) { *count = 2; return 0; } out[0] = vk_instance_extensions[1]; out[1] = vk_instance_extensions[2]; return 0;
    }
};
}}
@DEVICE_FUNCTION@
@INSTANCE_FUNCTION@
int main() {
    vk_device_extension_table device = {};
    get_device_extensions(nullptr, &device);
    assert(!device.extensions[0] && device.extensions[1]);
    auto *instance = get_instance_extensions();
    assert(instance->extensions[0] && !instance->extensions[1] && instance->extensions[2]);
}
