#include "query.h"
#include "host-common/opengl/NativeGpuInfo.h"
#include <dlfcn.h>
#include <cstdio>
#include <mutex>

namespace rme {
namespace {
std::mutex statusMutex;
GpuInfoResult last{GpuInfoStatus::NotQueried, VK_SUCCESS};
std::mutex nativeMutex;
const GpuInfoList* nativeList = nullptr;
GpuInfoResult nativeResult{GpuInfoStatus::NotQueried, VK_SUCCESS};
void record(GpuInfoResult result) {
    std::lock_guard<std::mutex> guard(statusMutex);
    last = result;
}
}
static GpuInfoResult queryWithLoader(const DynamicLoaderFunctions& loader, GpuInfoList* out) {
    if (!out) return {GpuInfoStatus::InvalidArgument, VK_SUCCESS};
    out->infos.clear();
    if (!loader.open || !loader.symbol || !loader.close)
        return {GpuInfoStatus::InvalidArgument, VK_SUCCESS};
    void* handle = loader.open("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) return {GpuInfoStatus::LoaderUnavailable, VK_SUCCESS};
    struct LibraryGuard {
        int (*close)(void*);
        void* handle;
        ~LibraryGuard() { if (handle) close(handle); }
        int closeNow() { void* h = handle; handle = nullptr; return close(h); }
    } guard{loader.close, handle};
    VulkanInfoFunctions f{
        reinterpret_cast<PFN_vkCreateInstance>(loader.symbol(handle, "vkCreateInstance")),
        reinterpret_cast<PFN_vkDestroyInstance>(loader.symbol(handle, "vkDestroyInstance")),
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(loader.symbol(handle, "vkEnumeratePhysicalDevices")),
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(loader.symbol(handle, "vkGetPhysicalDeviceProperties")),
    };
    auto result = queryVulkanGpuInfo(f, out);
    if (guard.closeNow() != 0) {
        out->infos.clear();
        return {GpuInfoStatus::LoaderCloseFailed, result.vkResult};
    }
    return result;
}
GpuInfoResult queryGpuInfoWithDynamicLoader(const DynamicLoaderFunctions& loader, GpuInfoList* out) {
    auto result = queryWithLoader(loader, out);
    record(result);
    return result;
}
GpuInfoResult queryAndroidLoaderGpuInfo(GpuInfoList* out) {
    DynamicLoaderFunctions loader{dlopen, dlsym, dlclose};
    return queryGpuInfoWithDynamicLoader(loader, out);
}
GpuInfoResult lastAndroidGpuInfoResult() {
    std::lock_guard<std::mutex> guard(statusMutex);
    return last;
}
GpuInfoResult queryNativeGpuInfoWithDynamicLoader(const DynamicLoaderFunctions& loader, GpuInfoList* out) {
    // Serializes queries through the native entry and protects list/result
    // association; a result for another list is never accepted.
    std::lock_guard<std::mutex> guard(nativeMutex);
    nativeList = out;
    nativeResult = {GpuInfoStatus::NotQueried, VK_SUCCESS};
    nativeResult = queryGpuInfoWithDynamicLoader(loader, out);
    return nativeResult;
}
GpuInfoResult nativeGpuInfoResultFor(const GpuInfoList* out) {
    std::lock_guard<std::mutex> guard(nativeMutex);
    return out && out == nativeList ? nativeResult : GpuInfoResult{GpuInfoStatus::NotQueried, VK_SUCCESS};
}
}

// gfxstream's fixed interface returns void; callers must inspect the separate
// result channel. This function never provides a Vulkan safety decision.
void getGpuInfoListNative(GpuInfoList* out) {
    rme::DynamicLoaderFunctions loader{dlopen, dlsym, dlclose};
    auto result = rme::queryNativeGpuInfoWithDynamicLoader(loader, out);
    if (result.status != rme::GpuInfoStatus::Ok)
        std::fprintf(stderr, "gfxstream Android Vulkan GPU query failed: status=%d VkResult=%d\n",
                     static_cast<int>(result.status), static_cast<int>(result.vkResult));
}
