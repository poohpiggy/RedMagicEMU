/* CPU-only test for the local window to remote Vulkan surface pairing. */
#include "android_surface.h"
#include <assert.h>
#include <stdint.h>

#ifndef RME_SURFACE_REMOTE_IMPLEMENTED
/* Model the current public surface path, which creates only a local object. */
struct RmeRemoteSurfaceOps {
    VkResult (*create)(VkInstance, VkSurfaceKHR *);
    void (*destroy)(VkInstance, VkSurfaceKHR);
};
static VkResult rme_android_surface_create_remote_vk(VkInstance instance, struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, const struct RmeRemoteSurfaceOps *ops, VkSurfaceKHR *out) {
    (void)instance; (void)ops;
    return rme_android_surface_create_vk(window, allocator, out);
}
static int rme_android_surface_remote_vk(VkSurfaceKHR local, VkSurfaceKHR *remote) {
    (void)local; (void)remote; return 0;
}
#endif

static unsigned retains, releases, connects, disconnects, creates, destroys;
static int fail_create;
static VkInstance last_instance;
static VkSurfaceKHR last_surface;
static void retain(struct ANativeWindow *window) { assert(window); ++retains; }
static void release(struct ANativeWindow *window) { assert(window); ++releases; }
static int usage(struct ANativeWindow *window, uint64_t *out) { assert(window); *out = 1; return 0; }
static int connect_window(struct ANativeWindow *window) { assert(window); ++connects; return 0; }
static int disconnect_window(struct ANativeWindow *window) { assert(window); ++disconnects; return 0; }
static const struct RmeAndroidWindowOps window_ops = {retain, release, usage, connect_window, disconnect_window};
const struct RmeAndroidWindowOps *rme_android_window_ops(void) { return &window_ops; }
static VkResult create_remote(VkInstance instance, VkSurfaceKHR *out) {
    ++creates;
    last_instance = instance;
    if (fail_create) return VK_ERROR_SURFACE_LOST_KHR;
    *out = (VkSurfaceKHR)(uintptr_t)0x1234;
    return VK_SUCCESS;
}
static void destroy_remote(VkInstance instance, VkSurfaceKHR surface) {
    ++destroys;
    last_instance = instance;
    last_surface = surface;
}
static const struct RmeRemoteSurfaceOps remote_ops = {create_remote, destroy_remote};

int main(void) {
    struct ANativeWindow *window = (struct ANativeWindow *)(uintptr_t)1;
    VkInstance instance = (VkInstance)(uintptr_t)2;
    VkSurfaceKHR local = VK_NULL_HANDLE;
    VkSurfaceKHR remote = VK_NULL_HANDLE;
    fail_create = 1;
    assert(rme_android_surface_create_remote_vk(instance, window, NULL, &remote_ops, &local) == VK_ERROR_SURFACE_LOST_KHR);
    assert(local == VK_NULL_HANDLE && creates == 1 && destroys == 0);
    assert(retains == 1 && releases == 1 && connects == 1 && disconnects == 1);
    fail_create = 0;
    assert(rme_android_surface_create_remote_vk(instance, window, NULL, &remote_ops, &local) == VK_SUCCESS);
    assert(local != VK_NULL_HANDLE && creates == 2);
    assert(rme_android_surface_remote_vk(local, &remote) && remote == (VkSurfaceKHR)(uintptr_t)0x1234);
    assert(local != remote && last_instance == instance);
    assert(rme_android_surface_destroy_vk(local));
    assert(destroys == 1 && last_instance == instance && last_surface == remote);
    assert(!rme_android_surface_remote_vk(local, &remote) && !rme_android_surface_destroy_vk(local));
    assert(retains == 2 && releases == 2 && connects == 2 && disconnects == 2);
}
