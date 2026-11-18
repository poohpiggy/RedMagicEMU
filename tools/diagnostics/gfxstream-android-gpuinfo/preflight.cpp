#include "approved_gpu_identity.h"
#include "host-common/opengl/NativeGpuInfo.h"
#include "host-common/opengl/gpuinfo.h"
#include "query.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "host_fixed_compute.h"
}

namespace {
using gfxstream::vk::ApprovedVulkanIdentity;

struct ProbeResult {
    bool passed;
    ApprovedVulkanIdentity identity;
};

ProbeResult runPreflight() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "RedMagicEMU Vulkan preflight";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan preflight instance creation failed (%d)\n", result);
        return {};
    }

    rme_vk_gpu_device gpu{};
    const auto opened = rme_vk_gpu_open(instance, vkGetInstanceProcAddr, &gpu);
    if (opened != RME_VK_DEVICE_OK) {
        std::fprintf(stderr, "Vulkan preflight device unavailable (%d, selection %d)\n",
                     opened, gpu.select_status);
        vkDestroyInstance(instance, nullptr);
        return {};
    }

    VkPhysicalDeviceIDProperties ids{};
    ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(gpu.selected.device, &properties);
    const auto hasUuid = [](const uint8_t (&uuid)[VK_UUID_SIZE]) {
        return std::any_of(uuid, uuid + VK_UUID_SIZE, [](uint8_t byte) { return byte != 0; });
    };
    const bool hasIdentity = hasUuid(ids.deviceUUID) && hasUuid(ids.driverUUID);
    if (!hasIdentity) std::fprintf(stderr, "Vulkan preflight device or driver UUID unavailable\n");

    rme_vk_fixed_status computed = RME_VK_FIXED_FAILED;
    if (hasIdentity) {
        uint32_t input[RME_VK_FIXED_WORDS];
        uint32_t output[RME_VK_FIXED_WORDS];
        for (unsigned i = 0; i < RME_VK_FIXED_WORDS; ++i) input[i] = 0x9e3779b9u * i;
        computed = rme_vk_host_fixed_compute(instance, &gpu, input, output);
        if (computed != RME_VK_FIXED_OK)
            std::fprintf(stderr, "Vulkan preflight compute failed (%d)\n", computed);
    }
    // On an unresolved dispatch, only process exit may reclaim its resources.
    if (computed == RME_VK_FIXED_UNRESOLVED) return {};
    if (rme_vk_gpu_close(&gpu) != RME_VK_DEVICE_OK) {
        std::fprintf(stderr, "Vulkan preflight device cleanup failed\n");
        return {};
    }
    vkDestroyInstance(instance, nullptr);
    if (computed != RME_VK_FIXED_OK) return {};

    ApprovedVulkanIdentity identity{};
    static_assert(sizeof(identity.deviceUuid) == VK_UUID_SIZE && sizeof(identity.driverUuid) == VK_UUID_SIZE);
    std::memcpy(identity.deviceUuid, ids.deviceUUID, VK_UUID_SIZE);
    std::memcpy(identity.driverUuid, ids.driverUUID, VK_UUID_SIZE);
    return {true, identity};
}

const ProbeResult& preflight() {
    static const ProbeResult result = runPreflight();
    return result;
}
}

bool isVulkanSafeToUseNative() {
    return preflight().passed;
}

bool gfxstream::vk::getApprovedVulkanIdentity(ApprovedVulkanIdentity* output) {
    // Called from renderer selection after the upstream GPU worker returns.
    const auto& info = globalGpuInfoList();
    if (!output || info.blacklist_status || info.VulkanBlacklist_status ||
        rme::nativeGpuInfoResultFor(&info).status != rme::GpuInfoStatus::Ok) return false;
    const auto& result = preflight();
    if (!result.passed) return false;
    *output = result.identity;
    return true;
}
