/* Small Vulkan device lifecycle component; not hardware attestation. */
#ifndef RME_VULKAN_GPU_DEVICE_H
#define RME_VULKAN_GPU_DEVICE_H
#include "gpu_select.h"

enum rme_vk_device_status {
    RME_VK_DEVICE_OK = 0,
    RME_VK_DEVICE_INVALID_ARGUMENT,
    RME_VK_DEVICE_ALREADY_OPEN,
    RME_VK_DEVICE_SELECT_FAILED,
    RME_VK_DEVICE_FUNCTION_UNAVAILABLE,
    RME_VK_DEVICE_CREATE_FAILED,
    RME_VK_DEVICE_QUEUE_UNAVAILABLE,
    RME_VK_DEVICE_INVALID_STATE,
    RME_VK_DEVICE_BUFFERS_ACTIVE,
    RME_VK_DEVICE_SUBMISSIONS_ACTIVE,
    RME_VK_DEVICE_FEATURE_UNSUPPORTED
};
struct rme_vk_gpu_device {
    enum rme_vk_device_status status;
    enum rme_vk_select_status select_status;
    VkResult vk_result;
    struct rme_vk_gpu_selection selected;
    VkDevice device;
    VkQueue queue;
    PFN_vkDestroyDevice destroy;
    VkPhysicalDeviceFeatures enabled_features;
    uint32_t active_buffer_allocations;
    uint32_t active_submissions;
    VkBool32 submission_unusable;
    VkResult submission_error;
};
/* Initialize state to zero before first open. Instance must request Vulkan 1.2
 * or later and remain alive, without concurrent destruction, through close.
 * State exclusively owns its device: never copy it or zero it while active.
 * Caller serializes operations; before close it must finish submitted work and
 * destroy resources created using this device. Tracked submissions also block
 * close until disposed; externally submitted work is caller's responsibility. */
enum rme_vk_device_status rme_vk_gpu_open(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *state);
/* An explicit request covers only Vulkan 1.0 VkPhysicalDeviceFeatures, not
 * extension or 1.1/1.2 feature chains. Every nonzero requested VkBool32 is
 * normalized to VK_TRUE; unsupported members are rejected before device
 * creation. The caller must ensure the requested combination is valid under
 * Vulkan's feature dependencies. A NULL request is identical to gpu_open.
 * This capability check does not validate or authorize Guest shaders. */
enum rme_vk_device_status rme_vk_gpu_open_with_features(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, const VkPhysicalDeviceFeatures *requested,
    struct rme_vk_gpu_device *state);
/* Valid until state changes; NULL if no device was successfully opened. */
const VkPhysicalDeviceFeatures *rme_vk_gpu_enabled_features(const struct rme_vk_gpu_device *state);
enum rme_vk_device_status rme_vk_gpu_close(struct rme_vk_gpu_device *state);
#endif
