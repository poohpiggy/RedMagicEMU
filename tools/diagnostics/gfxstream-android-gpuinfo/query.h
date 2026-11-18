#pragma once
#include <vulkan/vulkan.h>
class GpuInfoList;
namespace rme {
enum class GpuInfoStatus {
    NotQueried, InvalidArgument, LoaderUnavailable, LoaderCloseFailed, MissingEntryPoint,
    CreateInstanceFailed, InvalidInstance, NoDevices, EnumerationFailed,
    EnumerationIncomplete, TooManyDevices, InvalidDevice, InvalidProperties, Ok
};
struct GpuInfoResult { GpuInfoStatus status; VkResult vkResult; };
struct VulkanInfoFunctions {
    PFN_vkCreateInstance create;
    PFN_vkDestroyInstance destroy;
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkGetPhysicalDeviceProperties properties;
};
// Enumerates real loader objects supplied by the caller. Never decides safety.
GpuInfoResult queryVulkanGpuInfo(const VulkanInfoFunctions&, GpuInfoList*);
struct DynamicLoaderFunctions {
    void* (*open)(const char*, int);
    void* (*symbol)(void*, const char*);
    int (*close)(void*);
};
GpuInfoResult queryGpuInfoWithDynamicLoader(const DynamicLoaderFunctions&, GpuInfoList*);
GpuInfoResult queryAndroidLoaderGpuInfo(GpuInfoList*);
GpuInfoResult lastAndroidGpuInfoResult();
// Records only the result produced by the native entry point, associated with
// its output list. A different or absent list always returns NotQueried.
GpuInfoResult queryNativeGpuInfoWithDynamicLoader(const DynamicLoaderFunctions&, GpuInfoList*);
GpuInfoResult nativeGpuInfoResultFor(const GpuInfoList*);
}
