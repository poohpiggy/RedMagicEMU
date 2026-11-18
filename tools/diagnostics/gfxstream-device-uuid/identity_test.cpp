// Includes the actual device-selection block extracted from fixed d7a source.
// All device data is synthetic; no Vulkan loader or GPU is accessed.
#include "approved_gpu_identity.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#define VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1
#define VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 2
#define VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU 3
#define VK_PHYSICAL_DEVICE_TYPE_CPU 4
#define VK_API_VERSION_1_2 ((1u << 22) | (2u << 12))
#define VK_API_VERSION_VARIANT(v) ((v) >> 29)
#define VK_UUID_SIZE 16
#define VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(...) return -1

namespace gfxstream { namespace vk {
static ApprovedVulkanIdentity identity{};
static bool available = true;
bool getApprovedVulkanIdentity(ApprovedVulkanIdentity* out) {
    if (!available) return false;
    *out = identity;
    return true;
}
struct Info {
    bool hasGraphicsQueueFamily = true;
    bool supportsExternalMemoryImport = true, supportsExternalMemoryExport = false;
    bool supportsIdProperties = true;
    struct { uint32_t deviceType, apiVersion; } physdevProps;
    struct { uint8_t deviceUUID[16], driverUUID[16]; } idProps{};
    std::vector<uint32_t> graphicsQueueFamilyIndices{0}, computeQueueFamilyIndices{0};
};
struct Emu { bool instanceSupportsExternalMemoryCapabilities = true; };
static int select(std::vector<Info> deviceInfos) {
    uint32_t physdevCount = deviceInfos.size();
    Emu emu;
    auto* sVkEmulation = &emu;
#include "selection.inc"
    return 100 * static_cast<int>(maxScoringIndex) + selectedQueueFamilyIndex;
}
static Info info(uint8_t device, uint8_t driver, uint32_t type) {
    Info i;
    i.physdevProps = {type, VK_API_VERSION_1_2};
    std::memset(i.idProps.deviceUUID, device, 16);
    std::memset(i.idProps.driverUUID, driver, 16);
    return i;
}
static void approve(uint8_t device, uint8_t driver) {
    std::memset(identity.deviceUuid, device, 16);
    std::memset(identity.driverUuid, driver, 16);
}
}}
int main() {
    using namespace gfxstream::vk;
    auto a = info(1, 2, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
    auto b = info(3, 4, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    approve(1, 2);
    assert(select({a, b}) == 0); // Higher-scoring b must not override approved a.
    approve(3, 4);
    assert(select({a, b}) == 100);
    approve(1, 4);
    assert(select({a, b}) == -1);
    approve(1, 2);
    available = false;
    assert(select({a, b}) == -1);
    available = true;
    approve(0, 2);
    assert(select({a, b}) == -1);
    approve(1, 0);
    assert(select({a, b}) == -1);
    approve(1, 2);
    assert(select({a, a}) == -1);
    auto missing = a; missing.supportsIdProperties = false;
    assert(select({missing}) == -1);
    auto cpu = a; cpu.physdevProps.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    assert(select({cpu}) == -1);
    auto old = a; old.physdevProps.apiVersion = (1u << 22) | (1u << 12);
    assert(select({old}) == -1);
    auto separate = a; separate.computeQueueFamilyIndices = {1};
    assert(select({separate}) == -1);
}
