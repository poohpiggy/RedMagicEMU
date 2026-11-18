#include "host_guest_surface.h"
#include "surface_jni.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct ANativeWindow { int refs; };
static struct ANativeWindow local_window;
static VkInstance expected_instance;
static unsigned creates, destroys;
static VkResult create_result = VK_SUCCESS;
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject obj) {
    assert(env && obj);
    ++local_window.refs;
    return &local_window;
}
void ANativeWindow_acquire(ANativeWindow* w) { ++w->refs; }
void ANativeWindow_release(ANativeWindow* w) { assert(w->refs > 0); --w->refs; }
static VKAPI_ATTR VkResult VKAPI_CALL create(VkInstance i, const VkAndroidSurfaceCreateInfoKHR* info,
                                            const VkAllocationCallbacks* alloc, VkSurfaceKHR* out) {
    assert(i == expected_instance && info->window == &local_window && !alloc);
    ++creates;
    if (create_result == VK_SUCCESS) *out = (VkSurfaceKHR)(uintptr_t)(0x100 + creates);
    return create_result;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkInstance i, VkSurfaceKHR surface, const VkAllocationCallbacks* alloc) {
    assert(i == expected_instance && surface && !alloc);
    ++destroys;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i, const char* name) {
    assert(i == expected_instance);
    if (!strcmp(name, "vkCreateAndroidSurfaceKHR")) return (PFN_vkVoidFunction)create;
    if (!strcmp(name, "vkDestroySurfaceKHR")) return (PFN_vkVoidFunction)destroy;
    return NULL;
}
int main(void) {
    JNIEnv* env = (JNIEnv*)(uintptr_t)1;
    VkInstance first = (VkInstance)(uintptr_t)1, second = (VkInstance)(uintptr_t)2;
    VkSurfaceKHR surface = (VkSurfaceKHR)(uintptr_t)99;
    assert(rme_vk_guest_surface_create(first, get_proc, &surface) == VK_ERROR_SURFACE_LOST_KHR);
    assert(!surface && !creates);
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, (jobject)(uintptr_t)1));
    assert(local_window.refs == 1);
    expected_instance = first;
    assert(rme_vk_guest_surface_create(first, get_proc, &surface) == VK_SUCCESS);
    assert(surface && local_window.refs == 2 && creates == 1);
    assert(!rme_vk_guest_surface_destroy(second, surface) && destroys == 0);
    assert(rme_vk_guest_surface_destroy(first, surface) && local_window.refs == 1 && destroys == 1);
    assert(!rme_vk_guest_surface_destroy(first, surface));
    create_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(rme_vk_guest_surface_create(first, get_proc, &surface) == VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(!surface && local_window.refs == 1 && destroys == 1);
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, NULL));
    assert(local_window.refs == 0);
    puts("REAL_HOST_SURFACE_ADAPTER_LIFETIME_OK");
}
