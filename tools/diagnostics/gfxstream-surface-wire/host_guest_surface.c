/* Isolated EMU-owned Surface adapter; its caller synchronizes instance and
 * swapchain lifetime. Not connected to the app, transport, or decoder. */
#include "host_guest_surface.h"
#include "surface_jni.h"
#include <pthread.h>
#include <stdlib.h>

struct guest_surface {
    VkInstance instance;
    VkSurfaceKHR handle;
    RmeVkHostSurface* surface;
    struct guest_surface* next;
};
static pthread_mutex_t surface_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct guest_surface* surfaces;

VkResult rme_vk_guest_surface_create(VkInstance instance, PFN_vkGetInstanceProcAddr get_proc,
                                     VkSurfaceKHR* out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = VK_NULL_HANDLE;
    if (!instance || !get_proc) return VK_ERROR_INITIALIZATION_FAILED;
    RmeVkHostWindow* window = rme_vk_jni_acquire_window();
    if (!window) return VK_ERROR_SURFACE_LOST_KHR;
    struct guest_surface* entry = calloc(1, sizeof(*entry));
    if (!entry) { rme_vk_host_window_release(window); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    VkResult result = rme_vk_host_surface_create(instance, get_proc, window, &entry->surface);
    rme_vk_host_window_release(window);
    if (result != VK_SUCCESS) { free(entry); return result; }
    entry->instance = instance;
    entry->handle = rme_vk_host_surface_handle(entry->surface);
    VkSurfaceKHR handle = entry->handle;
    pthread_mutex_lock(&surface_mutex);
    entry->next = surfaces;
    surfaces = entry;
    pthread_mutex_unlock(&surface_mutex);
    *out = handle;
    return VK_SUCCESS;
}

int rme_vk_guest_surface_destroy(VkInstance instance, VkSurfaceKHR handle) {
    pthread_mutex_lock(&surface_mutex);
    struct guest_surface** link = &surfaces;
    while (*link && ((*link)->instance != instance || (*link)->handle != handle)) link = &(*link)->next;
    struct guest_surface* entry = *link;
    if (entry) *link = entry->next;
    pthread_mutex_unlock(&surface_mutex);
    if (!entry) return 0;
    rme_vk_host_surface_destroy(entry->surface);
    free(entry);
    return 1;
}
