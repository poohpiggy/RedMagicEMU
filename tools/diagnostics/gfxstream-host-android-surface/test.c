#include "host_surface.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static VkInstance instance_a = (VkInstance)(uintptr_t)0x101;
static VkInstance instance_b = (VkInstance)(uintptr_t)0x202;
static VkInstance requested_instance;
static VkResult create_result;
static int creates, destroys, lookups, missing;
static ANativeWindow* expected_window;
void ANativeWindow_acquire(ANativeWindow* w) { ++w->refs; }
void ANativeWindow_release(ANativeWindow* w) { --w->refs; }
static VkResult fake_create(VkInstance i, const VkAndroidSurfaceCreateInfoKHR* ci,
                            const VkAllocationCallbacks* alloc, VkSurfaceKHR* out) {
    assert(i == requested_instance && ci->window == expected_window && alloc == NULL);
    assert(ci->sType == VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR);
    ++creates;
    if (create_result == VK_SUCCESS) *out = 0x888;
    return create_result;
}
static void fake_destroy(VkInstance i, VkSurfaceKHR surface, const VkAllocationCallbacks* alloc) {
    assert(i == requested_instance && surface == 0x888 && alloc == NULL);
    ++destroys;
}
static PFN_vkVoidFunction fake_get(VkInstance i, const char* name) {
    assert(i == requested_instance);
    ++lookups;
    if (!strcmp(name, "vkCreateAndroidSurfaceKHR"))
        return missing == 1 ? NULL : (PFN_vkVoidFunction)fake_create;
    if (!strcmp(name, "vkDestroySurfaceKHR"))
        return missing == 2 ? NULL : (PFN_vkVoidFunction)fake_destroy;
    assert(0);
    return NULL;
}
int main(void) {
    ANativeWindow native = {1}; expected_window = &native;
    RmeVkHostWindow* window = rme_vk_host_window_acquire(&native);
    assert(window && native.refs == 2);
    RmeVkHostSurface* surface = NULL;
    requested_instance = instance_a;
    assert(rme_vk_host_surface_create(instance_a, fake_get, window, &surface) == VK_SUCCESS);
    assert(surface && native.refs == 3 && creates == 1 && lookups == 2);
    assert(rme_vk_host_surface_handle(surface) == 0x888);
    rme_vk_host_window_release(window);
    assert(native.refs == 2);
    rme_vk_host_surface_destroy(surface);
    assert(native.refs == 1 && destroys == 1);
    window = rme_vk_host_window_acquire(&native);
    requested_instance = instance_b; missing = 1; surface = NULL;
    assert(rme_vk_host_surface_create(instance_b, fake_get, window, &surface) == VK_ERROR_EXTENSION_NOT_PRESENT);
    assert(!surface && creates == 1 && native.refs == 2);
    missing = 2;
    assert(rme_vk_host_surface_create(instance_b, fake_get, window, &surface) == VK_ERROR_EXTENSION_NOT_PRESENT);
    assert(!surface && creates == 1 && native.refs == 2);
    missing = 0; create_result = -19;
    assert(rme_vk_host_surface_create(instance_b, fake_get, window, &surface) == -19);
    assert(!surface && creates == 2 && native.refs == 2 && destroys == 1);
    create_result = VK_SUCCESS;
    assert(rme_vk_host_surface_create(instance_b, fake_get, window, &surface) == VK_SUCCESS);
    assert(surface && creates == 3 && native.refs == 3);
    rme_vk_host_surface_destroy(surface);
    assert(destroys == 2 && native.refs == 2);
    assert(rme_vk_host_surface_create(VK_NULL_HANDLE, fake_get, window, &surface) == VK_ERROR_INITIALIZATION_FAILED);
    assert(rme_vk_host_surface_create(instance_b, NULL, window, &surface) == VK_ERROR_INITIALIZATION_FAILED);
    assert(rme_vk_host_surface_create(instance_b, fake_get, window, NULL) == VK_ERROR_INITIALIZATION_FAILED);
    assert(rme_vk_host_window_acquire(NULL) == NULL);
    rme_vk_host_window_release(window);
    assert(native.refs == 1);
}
