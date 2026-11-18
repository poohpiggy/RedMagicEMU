/* CPU-only window lifecycle test: callbacks do not access Android or a GPU. */
#include "android_surface.h"
#include <assert.h>
#include <stdlib.h>

static int retained, released, connected, disconnected, fail_usage, fail_connect;
static int allocated, freed, fail_allocation;
static void retain(struct ANativeWindow *w) { assert(w); retained++; }
static void release(struct ANativeWindow *w) { assert(w); released++; }
static int usage(struct ANativeWindow *w, uint64_t *value) {
    assert(w); *value = 123; return fail_usage ? -1 : 0;
}
static int connect_window(struct ANativeWindow *w) { assert(w); connected++; return fail_connect ? -1 : 0; }
static int disconnect_window(struct ANativeWindow *w) { assert(w); disconnected++; return 0; }
static void *alloc(void *user, size_t size, size_t alignment, VkSystemAllocationScope scope) {
    (void)user; assert(alignment <= _Alignof(max_align_t)); assert(scope == VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    allocated++; return fail_allocation ? NULL : malloc(size);
}
static void free_mem(void *user, void *p) { (void)user; freed++; free(p); }
static const struct RmeAndroidWindowOps ops = { retain, release, usage, connect_window, disconnect_window };
const struct RmeAndroidWindowOps *rme_android_window_ops(void) { return &ops; }
int main(void) {
    struct ANativeWindow *window = (struct ANativeWindow *)1;
    struct RmeAndroidSurface *surface = NULL;
    VkAllocationCallbacks allocator = {0};
    allocator.pfnAllocation = alloc;
    allocator.pfnFree = free_mem;
    fail_allocation = 1;
    assert(rme_android_surface_create(window, &allocator, &ops, &surface) == VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(!surface && retained == 0 && released == 0 && freed == 0);
    fail_allocation = 0;
    fail_usage = 1;
    assert(rme_android_surface_create(window, &allocator, &ops, &surface) == VK_ERROR_SURFACE_LOST_KHR);
    assert(!surface && retained == 1 && released == 1 && connected == 0 && freed == 1);
    fail_usage = 0; fail_connect = 1;
    assert(rme_android_surface_create(window, &allocator, &ops, &surface) == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    assert(!surface && retained == 2 && released == 2 && connected == 1 && disconnected == 0 && freed == 2);
    fail_connect = 0;
    assert(rme_android_surface_create(window, &allocator, &ops, &surface) == VK_SUCCESS);
    assert(surface && rme_android_surface_window(surface) == window && rme_android_surface_usage(surface) == 123);
    rme_android_surface_destroy(surface);
    rme_android_surface_destroy(NULL);
    assert(retained == 3 && released == 3 && connected == 2 && disconnected == 1);
    assert(allocated == 4 && freed == 3);
    assert(rme_android_surface_create(window, NULL, &ops, &surface) == VK_SUCCESS);
    rme_android_surface_destroy(surface);
    assert(retained == 4 && released == 4 && connected == 3 && disconnected == 2);
    VkSurfaceKHR handle = VK_NULL_HANDLE;
    assert(rme_android_surface_create_vk(window, &allocator, &handle) == VK_SUCCESS);
    assert(handle != VK_NULL_HANDLE);
    assert(rme_android_surface_destroy_vk((VkSurfaceKHR)(uintptr_t)1) == 0);
    assert(rme_android_surface_destroy_vk(handle) == 1);
    assert(rme_android_surface_destroy_vk(handle) == 0);
    assert(retained == 5 && released == 5 && connected == 4 && disconnected == 3);
}
