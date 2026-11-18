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
    uint32_t api = ci->pApplicationInfo ? ci->pApplicationInfo->apiVersion : 0;
    fprintf(stderr, "ALIAS_CREATE api=%u.%u.%u instance=%p result=%d extensions=%u\n",
            VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api),
            result == VK_SUCCESS ? (void*)*instance : NULL, result, ci->enabledExtensionCount);
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i)
        fprintf(stderr, "ALIAS_EXTENSION instance=%p name=%s\n",
                result == VK_SUCCESS ? (void*)*instance : NULL, ci->ppEnabledExtensionNames[i]);
    fprintf(stderr, "TEST_SHIM_CREATE result=%d instance=%p\n", result, result == VK_SUCCESS ? (void*)*instance : NULL);
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    PFN_vkDestroyInstance real = (PFN_vkDestroyInstance)dlsym(RTLD_NEXT, "vkDestroyInstance");
    if (!real) abort();
    fprintf(stderr, "TEST_SHIM_DESTROY instance=%p\n", (void*)instance);
    real(instance, allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* ci, const VkAllocationCallbacks* allocator, VkDevice* device) {
    PFN_vkCreateDevice real = (PFN_vkCreateDevice)dlsym(RTLD_NEXT, "vkCreateDevice");
    if (!real) abort();
    VkResult result=real(physical,ci,allocator,device);
    fprintf(stderr, "YCBCR_CREATE_DEVICE result=%d device=%p extensions=%u\n",result,result==VK_SUCCESS?(void*)*device:NULL,ci->enabledExtensionCount);
    for (uint32_t i=0;i<ci->enabledExtensionCount;++i) fprintf(stderr,"YCBCR_DEVICE_EXTENSION %s\n",ci->ppEnabledExtensionNames[i]);
    return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    PFN_vkDestroyDevice real = (PFN_vkDestroyDevice)dlsym(RTLD_NEXT,"vkDestroyDevice");
    if (!real) abort();
    fprintf(stderr,"YCBCR_DESTROY_DEVICE %p\n",(void*)device);
    real(device,allocator);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    PFN_vkGetDeviceProcAddr real=(PFN_vkGetDeviceProcAddr)dlsym(RTLD_NEXT,"vkGetDeviceProcAddr");
    if (!real) abort();
    const char* hidden=getenv("RME_HIDE_PROPERTIES2");
    int is_core=!strcmp(name,"vkCreateSamplerYcbcrConversion") || !strcmp(name,"vkDestroySamplerYcbcrConversion");
    int is_khr=!strcmp(name,"vkCreateSamplerYcbcrConversionKHR") || !strcmp(name,"vkDestroySamplerYcbcrConversionKHR");
    if (hidden && ((is_core && (!strcmp(hidden,"core") || !strcmp(hidden,"both"))) ||
                   (is_khr && (!strcmp(hidden,"khr") || !strcmp(hidden,"both"))))) {
        fprintf(stderr,"YCBCR_HIDDEN %s\n",name);
        return NULL;
    }
    if (!strcmp(name,"vkDestroyDevice")) return (PFN_vkVoidFunction)vkDestroyDevice;
    PFN_vkVoidFunction result=real(device,name);
    if (is_core || is_khr) fprintf(stderr,"YCBCR_QUERY name=%s real_available=%s\n",name,result?"yes":"no");
    return result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    static PFN_vkGetInstanceProcAddr real;
    if (!real) {
        real = (PFN_vkGetInstanceProcAddr)dlsym(RTLD_NEXT, "vkGetInstanceProcAddr");
        if (!real) { fputs("TEST_SHIM_NO_SYSTEM_GIPA\n", stderr); abort(); }
    }
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)vkCreateDevice;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)vkDestroyDevice;
    if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)vkDestroyInstance;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    PFN_vkVoidFunction result = real(instance, name);
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2") || !strcmp(name, "vkGetPhysicalDeviceProperties2KHR"))
        fprintf(stderr, "ALIAS_QUERY instance=%p name=%s real_available=%s\n",
                (void*)instance, name, result ? "yes" : "no");
    return result;
}
