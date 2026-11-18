#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define VK_COMMON_VERBOSE(...) ((void)0)
#define VK_COMMON_LOG(...) ((void)0)
#include "extracted_extensions.inc"
static VkExtensionProperties ext(const char* name) {
    VkExtensionProperties result{};
    std::strcpy(result.extensionName, name);
    return result;
}
int main() {
#include "extracted_requirements.inc"
    assert(!extensionsSupported({}, androidSurfaceInstanceExtNames));
    assert(!extensionsSupported({ext("VK_KHR_surface")}, androidSurfaceInstanceExtNames));
    assert(!extensionsSupported({ext("VK_KHR_android_surface")}, androidSurfaceInstanceExtNames));
    assert(extensionsSupported({ext("VK_KHR_surface"), ext("VK_KHR_android_surface")}, androidSurfaceInstanceExtNames));
    std::puts("ANDROID_SURFACE_EXTENSION_REQUIREMENTS_OK");
}
