/* Vulkan physical-device selection; reported properties are not hardware attestation. */
#ifndef RME_VULKAN_GPU_SELECT_H
#define RME_VULKAN_GPU_SELECT_H
#include <vulkan/vulkan.h>
#include <stdint.h>

enum rme_vk_select_status {
    RME_VK_SELECT_OK = 0,
    RME_VK_SELECT_INVALID_ARGUMENT,
    RME_VK_SELECT_FUNCTION_UNAVAILABLE,
    RME_VK_SELECT_ENUMERATION_FAILED,
    RME_VK_SELECT_LIMIT_EXCEEDED,
    RME_VK_SELECT_INVALID_RESPONSE,
    RME_VK_SELECT_NO_SUITABLE_DEVICE
};
struct rme_vk_gpu_selection {
    enum rme_vk_select_status status;
    VkResult vk_result;
    VkPhysicalDevice device;
    uint32_t queue_family;
    VkPhysicalDeviceProperties properties;
};
/* Caller supplies an instance created for Vulkan >= 1.2 and must prevent its
 * destruction during this call and while using the returned handle. The
 * returned handle belongs to instance; this function creates no device. On
 * the first eligible device, prefer a nonempty queue family with both graphics
 * and compute; otherwise select the first nonempty compute queue family.
 * The returned family is not guaranteed to support graphics. */
enum rme_vk_select_status rme_vk_select_gpu(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr, struct rme_vk_gpu_selection *out);
#endif
