#include "query.h"
#include "host-common/opengl/gpuinfo.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace {
int instancesCreated = 0, instancesDestroyed = 0, enumerations = 0;
VkResult createResult = VK_SUCCESS, enumerateResult = VK_SUCCESS;
uint32_t deviceCount = 2;
bool incompleteOnce = false, nullDevice = false;
int fakeInstance, fakeDevice[2];
int fakeLibrary, libraryCloses = 0, closeResult = 0;
bool noLibrary = false, noPropertySymbol = false;
VKAPI_ATTR VkResult VKAPI_CALL create(const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* out) {
    assert(info && info->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ++instancesCreated;
    if (createResult != VK_SUCCESS) return createResult;
    *out = reinterpret_cast<VkInstance>(&fakeInstance);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy(VkInstance instance, const VkAllocationCallbacks*) {
    assert(instance == reinterpret_cast<VkInstance>(&fakeInstance));
    ++instancesDestroyed;
}
VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance, uint32_t* count, VkPhysicalDevice* devices) {
    ++enumerations;
    if (enumerateResult != VK_SUCCESS) return enumerateResult;
    if (!devices) { *count = deviceCount; return VK_SUCCESS; }
    if (incompleteOnce) { incompleteOnce = false; return VK_INCOMPLETE; }
    assert(*count >= deviceCount);
    for (uint32_t i = 0; i < deviceCount; ++i)
        devices[i] = nullDevice ? VK_NULL_HANDLE : reinterpret_cast<VkPhysicalDevice>(&fakeDevice[i]);
    *count = deviceCount;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice device, VkPhysicalDeviceProperties* out) {
    unsigned index = device == reinterpret_cast<VkPhysicalDevice>(&fakeDevice[0]) ? 0 : 1;
    assert(device == reinterpret_cast<VkPhysicalDevice>(&fakeDevice[index]));
    std::memset(out, 0, sizeof(*out));
    out->vendorID = index ? 0x1002 : 0x5143;
    out->deviceID = index ? 0x44 : 0x1234;
    out->driverVersion = index ? 0x10203 : 0x6007008;
    std::strcpy(out->deviceName, index ? "Second test GPU" : "First test GPU");
}
void* openLibrary(const char* name, int flags) {
    assert(!std::strcmp(name, "libvulkan.so") && (flags & RTLD_NOW));
    return noLibrary ? nullptr : &fakeLibrary;
}
void* getSymbol(void* lib, const char* name) {
    assert(lib == &fakeLibrary);
    if (!std::strcmp(name, "vkCreateInstance")) return reinterpret_cast<void*>(create);
    if (!std::strcmp(name, "vkDestroyInstance")) return reinterpret_cast<void*>(destroy);
    if (!std::strcmp(name, "vkEnumeratePhysicalDevices")) return reinterpret_cast<void*>(enumerate);
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties"))
        return noPropertySymbol ? nullptr : reinterpret_cast<void*>(properties);
    return nullptr;
}
int closeLibrary(void* lib) { assert(lib == &fakeLibrary); ++libraryCloses; return closeResult; }
}
int main() {
    using namespace rme;
    GpuInfoList list;
    list.infos.emplace_back(); // Verify failure clears stale data.
    VulkanInfoFunctions f{create, destroy, enumerate, properties};
    auto missing = f; missing.properties = nullptr;
    assert(queryVulkanGpuInfo(missing, &list).status == GpuInfoStatus::MissingEntryPoint);
    assert(list.infos.empty() && instancesCreated == 0);
    createResult = VK_ERROR_INITIALIZATION_FAILED;
    auto result = queryVulkanGpuInfo(f, &list);
    assert(result.status == GpuInfoStatus::CreateInstanceFailed && result.vkResult == createResult);
    assert(instancesDestroyed == 0 && list.infos.empty());
    createResult = VK_SUCCESS;
    deviceCount = 0;
    assert(queryVulkanGpuInfo(f, &list).status == GpuInfoStatus::NoDevices);
    assert(instancesDestroyed == 1 && list.infos.empty());
    deviceCount = 2;
    enumerateResult = VK_ERROR_DEVICE_LOST;
    result = queryVulkanGpuInfo(f, &list);
    assert(result.status == GpuInfoStatus::EnumerationFailed && result.vkResult == VK_ERROR_DEVICE_LOST);
    assert(instancesDestroyed == 2 && list.infos.empty());
    enumerateResult = VK_SUCCESS;
    nullDevice = true;
    assert(queryVulkanGpuInfo(f, &list).status == GpuInfoStatus::InvalidDevice);
    assert(instancesDestroyed == 3 && list.infos.empty());
    nullDevice = false;
    incompleteOnce = true;
    assert(queryVulkanGpuInfo(f, &list).status == GpuInfoStatus::Ok);
    assert(instancesDestroyed == 4 && list.infos.size() == 2);
    assert(list.infos[0].make == "5143" && list.infos[0].device_id == "1234");
    assert(list.infos[0].model == "First test GPU" && list.infos[0].version == "0x06007008");
    assert(list.infos[1].make == "1002" && list.infos[1].device_id == "0044");
    for (const auto& info : list.infos) {
        assert(info.os == "A" && info.renderer.empty() && info.revision_id.empty() && !info.current_gpu);
    }
    assert(lastAndroidGpuInfoResult().status == GpuInfoStatus::NotQueried);
    DynamicLoaderFunctions loader{openLibrary, getSymbol, closeLibrary};
    noLibrary = true;
    assert(queryGpuInfoWithDynamicLoader(loader, &list).status == GpuInfoStatus::LoaderUnavailable);
    assert(lastAndroidGpuInfoResult().status == GpuInfoStatus::LoaderUnavailable);
    assert(list.infos.empty() && libraryCloses == 0);
    noLibrary = false;
    noPropertySymbol = true;
    assert(queryGpuInfoWithDynamicLoader(loader, &list).status == GpuInfoStatus::MissingEntryPoint);
    assert(lastAndroidGpuInfoResult().status == GpuInfoStatus::MissingEntryPoint);
    assert(list.infos.empty() && libraryCloses == 1);
    noPropertySymbol = false;
    closeResult = -1;
    assert(queryGpuInfoWithDynamicLoader(loader, &list).status == GpuInfoStatus::LoaderCloseFailed);
    assert(lastAndroidGpuInfoResult().status == GpuInfoStatus::LoaderCloseFailed);
    assert(list.infos.empty() && libraryCloses == 2);
    closeResult = 0;
    assert(queryGpuInfoWithDynamicLoader(loader, &list).status == GpuInfoStatus::Ok);
    assert(lastAndroidGpuInfoResult().status == GpuInfoStatus::Ok);
    assert(list.infos.size() == 2 && libraryCloses == 3);
    GpuInfoList other;
    assert(nativeGpuInfoResultFor(&list).status == GpuInfoStatus::NotQueried);
    noLibrary = true;
    assert(queryNativeGpuInfoWithDynamicLoader(loader, &list).status == GpuInfoStatus::LoaderUnavailable);
    assert(nativeGpuInfoResultFor(&list).status == GpuInfoStatus::LoaderUnavailable);
    assert(nativeGpuInfoResultFor(&other).status == GpuInfoStatus::NotQueried);
    noLibrary = false;
    assert(queryNativeGpuInfoWithDynamicLoader(loader, &other).status == GpuInfoStatus::Ok);
    assert(nativeGpuInfoResultFor(&other).status == GpuInfoStatus::Ok);
    assert(nativeGpuInfoResultFor(&list).status == GpuInfoStatus::NotQueried);
    assert(nativeGpuInfoResultFor(nullptr).status == GpuInfoStatus::NotQueried);
    std::puts("SYNTHETIC_VULKAN_GPUINFO_ENUMERATION_OK");
}
