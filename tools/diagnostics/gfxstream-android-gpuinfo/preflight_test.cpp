#include "approved_gpu_identity.h"
#include "host-common/opengl/NativeGpuInfo.h"
#include "host-common/opengl/gpuinfo.h"
#include "query.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include "host_fixed_compute.h"
}

static GpuInfoList gpuInfo;
static bool destroyed;
static const char* scenario;

const GpuInfoList& globalGpuInfoList() { return gpuInfo; }

rme::GpuInfoResult rme::nativeGpuInfoResultFor(const GpuInfoList* list) {
    assert(list == &gpuInfo);
    return {GpuInfoStatus::Ok, VK_SUCCESS};
}

#if defined(RME_TEST_FAKE_VULKAN)
static bool is(const char* name) { return std::strcmp(scenario, name) == 0; }
static VkInstance instance() { return reinterpret_cast<VkInstance>(1); }
static VkPhysicalDevice physical() { return reinterpret_cast<VkPhysicalDevice>(2); }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info,
                                               const VkAllocationCallbacks*, VkInstance* out) {
    assert(info->pApplicationInfo->apiVersion == VK_API_VERSION_1_2);
    *out = instance();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance handle, const VkAllocationCallbacks*) {
    assert(handle == instance());
    destroyed = true;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*) {
    return nullptr;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice device,
                                                       VkPhysicalDeviceProperties2* props) {
    assert(device == physical());
    auto* ids = static_cast<VkPhysicalDeviceIDProperties*>(props->pNext);
    assert(ids->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
    ids->deviceUUID[0] = 0x12;
    if (!is("empty-uuid")) ids->driverUUID[0] = 0x34;
}

extern "C" rme_vk_device_status rme_vk_gpu_open(VkInstance handle, PFN_vkGetInstanceProcAddr,
                                               rme_vk_gpu_device* gpu) {
    assert(handle == instance());
    gpu->selected.device = physical();
    return RME_VK_DEVICE_OK;
}

extern "C" rme_vk_device_status rme_vk_gpu_close(rme_vk_gpu_device* gpu) {
    assert(gpu->selected.device == physical());
    assert(!is("unresolved"));
    return RME_VK_DEVICE_OK;
}

extern "C" rme_vk_fixed_status rme_vk_host_fixed_compute(VkInstance handle, rme_vk_gpu_device* gpu,
                                                       const uint32_t* in, uint32_t* out) {
    assert(handle == instance() && gpu->selected.device == physical());
    assert(in[0] != in[63]);
    if (is("unresolved")) return RME_VK_FIXED_UNRESOLVED;
    if (is("compute-fail")) return RME_VK_FIXED_FAILED;
    for (unsigned i = 0; i < RME_VK_FIXED_WORDS; ++i) out[i] = 3u * in[i] + 7u;
    return RME_VK_FIXED_OK;
}
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    const bool available = isVulkanSafeToUseNative();
    gpuInfo.VulkanBlacklist_status = !available;
    gfxstream::vk::ApprovedVulkanIdentity ids{};
#if defined(RME_TEST_FAKE_VULKAN)
    const bool expectAvailable = is("ok") || is("blacklisted");
    assert(destroyed == !is("unresolved"));
    gpuInfo.blacklist_status = is("blacklisted");
    assert(available == expectAvailable);
    const bool approved = gfxstream::vk::getApprovedVulkanIdentity(&ids);
    assert(approved == is("ok"));
    if (approved) assert(ids.deviceUuid[0] == 0x12 && ids.driverUuid[0] == 0x34);
#else
    (void)destroyed;
    assert(!available);
    assert(!gfxstream::vk::getApprovedVulkanIdentity(&ids));
#endif
    std::printf("PREFLIGHT_%s_OK\n", scenario);
}
