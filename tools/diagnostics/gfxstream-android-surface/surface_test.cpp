#include "DisplaySurfaceVk.h"
#include <vulkan/vulkan_android.h>
#include <cassert>
#include <cstdio>
#include <cstring>

// Synthetic Vulkan dispatch only; never loads a driver or creates a window.
struct ANativeWindow { int dummy; };
static ANativeWindow fakeWindow;
static int fakeInstance, fakeSurface, creates, destroys;
static bool expose = true, fail = false;
static VKAPI_ATTR VkResult VKAPI_CALL createSurface(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* info,
                                                   const VkAllocationCallbacks* alloc, VkSurfaceKHR* out) {
    assert(instance == reinterpret_cast<VkInstance>(&fakeInstance));
    assert(info && info->sType == VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR &&
           !info->pNext && !info->flags && info->window == &fakeWindow && !alloc);
    ++creates;
    if (fail) return VK_ERROR_INITIALIZATION_FAILED;
    *out = reinterpret_cast<VkSurfaceKHR>(&fakeSurface);
    return VK_SUCCESS;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getProc(VkInstance instance, const char* name) {
    assert(instance == reinterpret_cast<VkInstance>(&fakeInstance));
    assert(!std::strcmp(name, "vkCreateAndroidSurfaceKHR"));
    return expose ? reinterpret_cast<PFN_vkVoidFunction>(createSurface) : nullptr;
}
static VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance instance, VkSurfaceKHR surface,
                                                 const VkAllocationCallbacks* alloc) {
    assert(instance == reinterpret_cast<VkInstance>(&fakeInstance));
    assert(surface == reinterpret_cast<VkSurfaceKHR>(&fakeSurface) && !alloc);
    ++destroys;
}
int main() {
    gfxstream::vk::VulkanDispatch dispatch{};
    dispatch.vkGetInstanceProcAddr = getProc;
    dispatch.vkDestroySurfaceKHR = destroySurface;
    auto instance = reinterpret_cast<VkInstance>(&fakeInstance);
    using gfxstream::vk::DisplaySurfaceVk;
    assert(!DisplaySurfaceVk::create(dispatch, instance, nullptr));
    expose = false;
    assert(!DisplaySurfaceVk::create(dispatch, instance, &fakeWindow) && !creates);
    expose = true; fail = true;
    assert(!DisplaySurfaceVk::create(dispatch, instance, &fakeWindow) && creates == 1);
    fail = false;
    {
        auto result = DisplaySurfaceVk::create(dispatch, instance, &fakeWindow);
        assert(result && result->getSurface() == reinterpret_cast<VkSurfaceKHR>(&fakeSurface));
    }
    assert(creates == 2 && destroys == 1);
    std::puts("ANDROID_SURFACE_SYNTHETIC_DISPATCH_OK");
}
