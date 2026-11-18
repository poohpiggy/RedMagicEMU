#include "query.h"
#include "host-common/opengl/gpuinfo.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace rme {
GpuInfoResult queryVulkanGpuInfo(const VulkanInfoFunctions& f, GpuInfoList* out) {
    if (!out) return {GpuInfoStatus::InvalidArgument, VK_SUCCESS};
    out->infos.clear();
    if (!f.create || !f.destroy || !f.enumerate || !f.properties)
        return {GpuInfoStatus::MissingEntryPoint, VK_SUCCESS};
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "RedMagicEMU GPU information";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = f.create(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) return {GpuInfoStatus::CreateInstanceFailed, result};
    if (!instance) return {GpuInfoStatus::InvalidInstance, VK_SUCCESS};
    struct InstanceGuard {
        PFN_vkDestroyInstance destroy;
        VkInstance instance;
        ~InstanceGuard() { destroy(instance, nullptr); }
    } guard{f.destroy, instance};
    constexpr uint32_t kMaxDevices = 256;
    constexpr unsigned kAttempts = 3;
    for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
        uint32_t count = 0;
        result = f.enumerate(instance, &count, nullptr);
        if (result != VK_SUCCESS) return {GpuInfoStatus::EnumerationFailed, result};
        if (!count) return {GpuInfoStatus::NoDevices, VK_SUCCESS};
        if (count > kMaxDevices) return {GpuInfoStatus::TooManyDevices, VK_SUCCESS};
        std::vector<VkPhysicalDevice> devices(count);
        const uint32_t capacity = count;
        result = f.enumerate(instance, &count, devices.data());
        if (result == VK_INCOMPLETE) continue;
        if (result != VK_SUCCESS) return {GpuInfoStatus::EnumerationFailed, result};
        if (!count) return {GpuInfoStatus::NoDevices, VK_SUCCESS};
        if (count > capacity) return {GpuInfoStatus::InvalidDevice, VK_SUCCESS};
        std::vector<GpuInfo> infos;
        infos.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!devices[i]) return {GpuInfoStatus::InvalidDevice, VK_SUCCESS};
            VkPhysicalDeviceProperties p{};
            f.properties(devices[i], &p);
            const auto* end = static_cast<const char*>(std::memchr(p.deviceName, '\0', sizeof(p.deviceName)));
            if (!end || end == p.deviceName) return {GpuInfoStatus::InvalidProperties, VK_SUCCESS};
            char vendor[16], device[16], version[16];
            std::snprintf(vendor, sizeof(vendor), "%04x", p.vendorID);
            std::snprintf(device, sizeof(device), "%04x", p.deviceID);
            std::snprintf(version, sizeof(version), "0x%08x", p.driverVersion);
            GpuInfo info;
            info.make = vendor;
            info.model.assign(p.deviceName, static_cast<size_t>(end - p.deviceName));
            info.device_id = device;
            info.version = version; // Vulkan's raw vendor-defined integer.
            info.os = "A";          // Android, not desktop Linux.
            // revision, GLES renderer and the renderer-selected GPU are unknown.
            infos.push_back(std::move(info));
        }
        out->infos = std::move(infos);
        return {GpuInfoStatus::Ok, VK_SUCCESS};
    }
    return {GpuInfoStatus::EnumerationIncomplete, VK_INCOMPLETE};
}
}
