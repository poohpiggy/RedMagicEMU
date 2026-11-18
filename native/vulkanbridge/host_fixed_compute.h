/* Fixed host-side GPU operation; never accepts external shader code. */
#ifndef RME_VULKAN_HOST_FIXED_COMPUTE_H
#define RME_VULKAN_HOST_FIXED_COMPUTE_H
#include "gpu_device.h"
#include <stdint.h>

enum { RME_VK_FIXED_WORDS = 64 };
enum rme_vk_fixed_status {
    RME_VK_FIXED_OK = 0,
    RME_VK_FIXED_FAILED,
    /* Submitted work may still own resources: caller must terminate process. */
    RME_VK_FIXED_UNRESOLVED
};
enum rme_vk_fixed_status rme_vk_host_fixed_compute(VkInstance instance,
    struct rme_vk_gpu_device *gpu, const uint32_t input[RME_VK_FIXED_WORDS],
    uint32_t output[RME_VK_FIXED_WORDS]);
#endif
