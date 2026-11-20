/* Android surface lifecycle follows the Android 16 Vulkan loader's swapchain
 * implementation, Copyright 2015 The Android Open Source Project,
 * licensed under the Apache License, Version 2.0. */
#include "android_surface.h"
#include <pthread.h>
#include <stdlib.h>

struct RmeAndroidSurface {
    struct ANativeWindow *window;
    struct RmeAndroidWindowOps ops;
    VkAllocationCallbacks allocator;
    uint64_t consumer_usage;
    int custom_allocator;
    VkInstance remote_instance;
    VkSurfaceKHR remote_surface;
    struct RmeRemoteSurfaceOps remote_ops;
    struct RmeAndroidSurface *next;
};

static pthread_mutex_t surface_lock = PTHREAD_MUTEX_INITIALIZER;
static struct RmeAndroidSurface *surfaces;

static void free_surface(struct RmeAndroidSurface *surface) {
    if (surface->custom_allocator) {
        surface->allocator.pfnFree(surface->allocator.pUserData, surface);
    } else {
        free(surface);
    }
}

VkResult rme_android_surface_create(struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, const struct RmeAndroidWindowOps *ops,
    struct RmeAndroidSurface **out) {
    *out = NULL;
    struct RmeAndroidSurface *surface = allocator
        ? allocator->pfnAllocation(allocator->pUserData, sizeof(*surface),
              _Alignof(struct RmeAndroidSurface), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)
        : malloc(sizeof(*surface));
    if (!surface) return VK_ERROR_OUT_OF_HOST_MEMORY;
    surface->window = window;
    surface->ops = *ops;
    surface->custom_allocator = allocator != NULL;
    surface->remote_instance = VK_NULL_HANDLE;
    surface->remote_surface = VK_NULL_HANDLE;
    surface->remote_ops = (struct RmeRemoteSurfaceOps){0};
    if (allocator) surface->allocator = *allocator;
    ops->retain(window);
    if (ops->consumer_usage(window, &surface->consumer_usage) != 0) {
        ops->release(window);
        free_surface(surface);
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (ops->connect(window) != 0) {
        ops->release(window);
        free_surface(surface);
        return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    }
    *out = surface;
    return VK_SUCCESS;
}

void rme_android_surface_destroy(struct RmeAndroidSurface *surface) {
    if (!surface) return;
    if (surface->remote_surface != VK_NULL_HANDLE)
        surface->remote_ops.destroy(surface->remote_instance, surface->remote_surface);
    surface->ops.disconnect(surface->window);
    surface->ops.release(surface->window);
    free_surface(surface);
}

struct ANativeWindow *rme_android_surface_window(const struct RmeAndroidSurface *surface) {
    return surface->window;
}

uint64_t rme_android_surface_usage(const struct RmeAndroidSurface *surface) {
    return surface->consumer_usage;
}

static void register_surface(struct RmeAndroidSurface *surface, VkSurfaceKHR *out) {
    pthread_mutex_lock(&surface_lock);
    surface->next = surfaces;
    surfaces = surface;
    pthread_mutex_unlock(&surface_lock);
    *out = (VkSurfaceKHR)(uintptr_t)surface;
}

VkResult rme_android_surface_create_vk(struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *out) {
    struct RmeAndroidSurface *surface;
    VkResult result = rme_android_surface_create(window, allocator, rme_android_window_ops(), &surface);
    if (result != VK_SUCCESS) return result;
    register_surface(surface, out);
    return VK_SUCCESS;
}

VkResult rme_android_surface_create_remote_vk(VkInstance instance, struct ANativeWindow *window,
    const VkAllocationCallbacks *allocator, const struct RmeRemoteSurfaceOps *ops, VkSurfaceKHR *out) {
    *out = VK_NULL_HANDLE;
    if (!ops || !ops->create || !ops->destroy) return VK_ERROR_INITIALIZATION_FAILED;
    struct RmeAndroidSurface *surface;
    VkResult result = rme_android_surface_create(window, allocator, rme_android_window_ops(), &surface);
    if (result != VK_SUCCESS) return result;
    VkSurfaceKHR remote = VK_NULL_HANDLE;
    result = ops->create(instance, &remote);
    if (result != VK_SUCCESS || remote == VK_NULL_HANDLE) {
        rme_android_surface_destroy(surface);
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    }
    surface->remote_instance = instance;
    surface->remote_surface = remote;
    surface->remote_ops = *ops;
    register_surface(surface, out);
    return VK_SUCCESS;
}

int rme_android_surface_destroy_vk(VkSurfaceKHR handle) {
    struct RmeAndroidSurface *surface = (struct RmeAndroidSurface *)(uintptr_t)handle;
    pthread_mutex_lock(&surface_lock);
    struct RmeAndroidSurface **link = &surfaces;
    while (*link && *link != surface) link = &(*link)->next;
    if (!*link) {
        pthread_mutex_unlock(&surface_lock);
        return 0;
    }
    *link = surface->next;
    pthread_mutex_unlock(&surface_lock);
    rme_android_surface_destroy(surface);
    return 1;
}

int rme_android_surface_owned_vk(VkSurfaceKHR handle) {
    struct RmeAndroidSurface *target = (struct RmeAndroidSurface *)(uintptr_t)handle;
    pthread_mutex_lock(&surface_lock);
    struct RmeAndroidSurface *surface = surfaces;
    while (surface && surface != target) surface = surface->next;
    int found = surface != NULL;
    pthread_mutex_unlock(&surface_lock);
    return found;
}

struct ANativeWindow *rme_android_surface_window_vk(VkSurfaceKHR handle) {
    struct RmeAndroidSurface *target = (struct RmeAndroidSurface *)(uintptr_t)handle;
    pthread_mutex_lock(&surface_lock);
    struct RmeAndroidSurface *surface = surfaces;
    while (surface && surface != target) surface = surface->next;
    struct ANativeWindow *window = surface ? surface->window : NULL;
    pthread_mutex_unlock(&surface_lock);
    return window;
}

int rme_android_surface_remote_info_vk(VkSurfaceKHR handle, VkInstance *instance, VkSurfaceKHR *remote) {
    struct RmeAndroidSurface *target = (struct RmeAndroidSurface *)(uintptr_t)handle;
    pthread_mutex_lock(&surface_lock);
    struct RmeAndroidSurface *surface = surfaces;
    while (surface && surface != target) surface = surface->next;
    int found = surface && surface->remote_surface != VK_NULL_HANDLE;
    if (found) { *instance = surface->remote_instance; *remote = surface->remote_surface; }
    pthread_mutex_unlock(&surface_lock);
    return found;
}

int rme_android_surface_remote_vk(VkSurfaceKHR handle, VkSurfaceKHR *remote) {
    VkInstance instance;
    return rme_android_surface_remote_info_vk(handle, &instance, remote);
}
