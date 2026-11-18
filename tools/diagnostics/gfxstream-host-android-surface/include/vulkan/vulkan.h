#ifndef TEST_VULKAN_H
#define TEST_VULKAN_H
#include <stdint.h>
#include <android/native_window.h>
typedef struct VkInstance_T* VkInstance;
typedef uint64_t VkSurfaceKHR;
typedef int32_t VkResult;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef struct VkAllocationCallbacks VkAllocationCallbacks;
typedef struct VkAndroidSurfaceCreateInfoKHR {
    int32_t sType;
    const void* pNext;
    uint32_t flags;
    ANativeWindow* window;
} VkAndroidSurfaceCreateInfoKHR;
typedef VkResult (*PFN_vkCreateAndroidSurfaceKHR)(VkInstance, const VkAndroidSurfaceCreateInfoKHR*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef void (*PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*);
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_NULL_HANDLE 0
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000
#endif
