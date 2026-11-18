#include "../surface_jni.h"

// The real gfxstream decoder is C++, while both surface helpers are C.
extern "C" int rme_surface_cpp_probe(JNIEnv* env, jobject surface) {
    if (!Java_app_aemu_core_VulkanSurface_nativeUpdate(env, nullptr, surface)) return 1;
    RmeVkHostWindow* window = rme_vk_jni_acquire_window();
    if (!window) return 2;
    RmeVkHostSurface* result = nullptr;
    VkResult status = rme_vk_host_surface_create(VK_NULL_HANDLE, nullptr, window, &result);
    if (status != VK_ERROR_INITIALIZATION_FAILED || result) return 3;
    if (rme_vk_host_surface_handle(nullptr) != VK_NULL_HANDLE) return 4;
    rme_vk_host_surface_destroy(nullptr);
    rme_vk_host_window_release(window);
    return Java_app_aemu_core_VulkanSurface_nativeUpdate(env, nullptr, nullptr) ? 0 : 5;
}
