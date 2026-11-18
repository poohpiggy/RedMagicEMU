/* One storage-buffer compute dispatch on an existing GPU device. */
#ifndef RME_VULKAN_GPU_COMPUTE_H
#define RME_VULKAN_GPU_COMPUTE_H
#include "gpu_buffer.h"
#include "gpu_submission.h"
#include <stddef.h>

enum rme_vk_compute_status {
    RME_VK_COMPUTE_OK = 0,
    RME_VK_COMPUTE_INVALID_ARGUMENT,
    RME_VK_COMPUTE_ALREADY_ACTIVE,
    RME_VK_COMPUTE_LIMIT_EXCEEDED,
    RME_VK_COMPUTE_FUNCTION_UNAVAILABLE,
    RME_VK_COMPUTE_VULKAN_ERROR,
    RME_VK_COMPUTE_SUBMIT_ERROR,
    RME_VK_COMPUTE_WAIT_ERROR,
    RME_VK_COMPUTE_TIMEOUT,
    RME_VK_COMPUTE_STILL_PENDING,
    RME_VK_COMPUTE_OUTCOME_UNKNOWN,
    RME_VK_COMPUTE_INVALID_STATE,
    RME_VK_COMPUTE_DEVICE_UNAVAILABLE
};
struct rme_vk_compute_dispatch {
    enum rme_vk_compute_status status;
    VkResult vk_result;
    struct rme_vk_gpu_device *gpu;
    struct rme_vk_gpu_buffer *buffer;
    struct rme_vk_gpu_submission submission;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkPipelineLayout pipeline_layout;
    VkShaderModule shader;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_layout;
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout;
    PFN_vkDestroyShaderModule destroy_shader;
    PFN_vkDestroyPipeline destroy_pipeline;
    PFN_vkDestroyCommandPool destroy_command_pool;
};
/* State exclusively owns its Vulkan objects: zero-initialize, do not copy or
 * clear while active. The instance/get_proc must be those used to open gpu.
 * Caller serializes access, keeps instance/device/buffer
 * alive, and supplies valid aligned Vulkan SPIR-V whose selected compute entry
 * uses only set 0 binding 0 as a storage buffer and accesses only its bounds.
 * Shader is trusted and caller-validated against device-enabled features and
 * extensions: local size must meet maxComputeWorkGroupSize and its product must
 * meet maxComputeWorkGroupInvocations. This API checks SPIR-V size, alignment
 * and magic only, not these properties, and must not receive untrusted Guest code.
 * Caller handles mapped-memory flush/invalidate and synchronization with all
 * other GPU producers/consumers and queues. Internal barriers cover only host
 * writes before this dispatch and shader writes before subsequent host reads.
 * Submit is asynchronous. Wait for success before consuming results. */
enum rme_vk_compute_status rme_vk_compute_start(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    struct rme_vk_gpu_buffer *buffer, const uint32_t *spirv, size_t words,
    const char *entry, uint32_t x, uint32_t y, uint32_t z,
    struct rme_vk_compute_dispatch *state);
enum rme_vk_compute_status rme_vk_compute_wait(struct rme_vk_compute_dispatch *state, uint64_t timeout_ns);
enum rme_vk_compute_status rme_vk_compute_dispose(struct rme_vk_compute_dispatch *state);
#endif
