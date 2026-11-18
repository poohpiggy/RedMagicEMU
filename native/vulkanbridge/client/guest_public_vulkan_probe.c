#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#ifdef RME_TEST_MAPPED_MEMORY
#include "guest_public_memory.h"
#endif

static int fail(const char *step, VkResult result) {
    fprintf(stderr, "GUEST_VULKAN_PUBLIC_FAILED %s result=%d\n", step, result);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return fail("expected driver path", VK_ERROR_INITIALIZATION_FAILED);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "GUEST_VULKAN_PUBLIC_FAILED dlopen %s\n", dlerror());
        return 1;
    }
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!get) return fail("vkGetInstanceProcAddr", VK_ERROR_INITIALIZATION_FAILED);

    PFN_vkEnumerateInstanceVersion version =
        (PFN_vkEnumerateInstanceVersion)get(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(VK_NULL_HANDLE, "vkCreateInstance");
    if (!version || !create) return fail("global dispatch", VK_ERROR_INITIALIZATION_FAILED);
    uint32_t api = 0;
    VkResult status = version(&api);
    if (status != VK_SUCCESS) return fail("vkEnumerateInstanceVersion", status);
    if (api < VK_API_VERSION_1_2) return fail("Vulkan 1.2 unavailable", VK_ERROR_INCOMPATIBLE_DRIVER);

    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "EMU Guest Vulkan ABI check",
        .apiVersion = VK_API_VERSION_1_2,
    };
    const VkInstanceCreateInfo config = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance;
#ifdef RME_TEST_INSTANCE_EXTENSIONS
    PFN_vkEnumerateInstanceExtensionProperties extensions =
        (PFN_vkEnumerateInstanceExtensionProperties)get(
            VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!extensions) return fail("extension dispatch", VK_ERROR_INITIALIZATION_FAILED);
    uint32_t extensionCount = 0;
    status = extensions(NULL, &extensionCount, NULL);
    if (status != VK_SUCCESS) return fail("count instance extensions", status);
    VkExtensionProperties *list = calloc(extensionCount, sizeof(*list));
    if (extensionCount && !list) return fail("allocate extension list", VK_ERROR_OUT_OF_HOST_MEMORY);
    status = extensions(NULL, &extensionCount, list);
    int found = 0;
    for (uint32_t i = 0; i < extensionCount; i++)
        found |= strcmp(list[i].extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0;
    free(list);
    if (status != VK_SUCCESS || !found)
        return fail("get_physical_device_properties2 extension", VK_ERROR_EXTENSION_NOT_PRESENT);
    printf("GUEST_VULKAN_INSTANCE_EXTENSIONS_OK count=%" PRIu32 "\n", extensionCount);
#endif
    status = create(&config, NULL, &instance);
    if (status != VK_SUCCESS) return fail("vkCreateInstance", status);

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)get(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties getProperties =
        (PFN_vkGetPhysicalDeviceProperties)get(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
    if (!enumerate || !getProperties || !destroy)
        return fail("instance dispatch", VK_ERROR_INITIALIZATION_FAILED);

    uint32_t count = 1;
    VkPhysicalDevice device;
    status = enumerate(instance, &count, &device);
    if (status != VK_SUCCESS || count != 1) {
        destroy(instance, NULL);
        return fail("vkEnumeratePhysicalDevices", status);
    }
    VkPhysicalDeviceProperties properties;
    getProperties(device, &properties);
#ifdef RME_TEST_MAPPED_MEMORY
    const char *loaderEntries[] = {
        "vkCreateSwapchainKHR", "vkDestroySwapchainKHR", "vkGetSwapchainImagesKHR",
        "vkAcquireNextImageKHR", "vkQueuePresentKHR",
    };
    for (size_t i = 0; i < sizeof(loaderEntries) / sizeof(loaderEntries[0]); ++i) {
        if (!get(instance, loaderEntries[i])) {
            destroy(instance, NULL);
            return fail(loaderEntries[i], VK_ERROR_INITIALIZATION_FAILED);
        }
    }
    const int testResult = testMappedBuffer(get, instance, device);
#endif
    destroy(instance, NULL);
#ifdef RME_TEST_MAPPED_MEMORY
    if (testResult) return testResult;
#endif
    if (properties.apiVersion < VK_API_VERSION_1_2 || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
        return fail("physical device unsuitable", VK_ERROR_FEATURE_NOT_PRESENT);
    printf("GUEST_VULKAN_PUBLIC_OK device=%s vendor=0x%04" PRIx32 " api=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
           properties.deviceName, properties.vendorID, VK_API_VERSION_MAJOR(properties.apiVersion),
           VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
    return 0;
}
