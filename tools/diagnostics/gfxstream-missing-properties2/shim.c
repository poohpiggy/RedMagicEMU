// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* allocator, VkInstance* instance) {
    PFN_vkCreateInstance real = (PFN_vkCreateInstance)dlsym(RTLD_NEXT, "vkCreateInstance");
    if (!real) abort();
    VkResult result = real(ci, allocator, instance);
    fprintf(stderr, "TEST_SHIM_CREATE result=%d instance=%p\n", result, result == VK_SUCCESS ? (void*)*instance : NULL);
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    PFN_vkDestroyInstance real = (PFN_vkDestroyInstance)dlsym(RTLD_NEXT, "vkDestroyInstance");
    if (!real) abort();
    fprintf(stderr, "TEST_SHIM_DESTROY instance=%p\n", (void*)instance);
    real(instance, allocator);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    static PFN_vkGetInstanceProcAddr real;
    if (!real) {
        real = (PFN_vkGetInstanceProcAddr)dlsym(RTLD_NEXT, "vkGetInstanceProcAddr");
        if (!real) { fputs("TEST_SHIM_NO_SYSTEM_GIPA\n", stderr); abort(); }
    }
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)vkDestroyInstance;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    const char* hidden = getenv("RME_HIDE_PROPERTIES2");
    if (hidden &&
        ((!strcmp(name, "vkGetPhysicalDeviceProperties2") && (!strcmp(hidden, "both") || !strcmp(hidden, "core"))) ||
         (!strcmp(name, "vkGetPhysicalDeviceProperties2KHR") && (!strcmp(hidden, "both") || !strcmp(hidden, "khr"))))) {
        fprintf(stderr, "TEST_SHIM_HIDDEN %s instance=%s\n", name, instance ? "nonnull" : "null");
        return NULL;
    }
    return real(instance, name);
}
