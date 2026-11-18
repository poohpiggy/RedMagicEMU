#pragma once
#include <cstdint>

namespace gfxstream { namespace vk {
struct ApprovedVulkanIdentity {
    uint8_t deviceUuid[16];
    uint8_t driverUuid[16];
};
// Required external provider: populate only after the existing GPU policy and
// capability checks approve the exact physical device. No default is supplied.
bool getApprovedVulkanIdentity(ApprovedVulkanIdentity* output);
}}
