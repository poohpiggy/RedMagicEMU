/* Vulkan buffer lifecycle, no mapping or synchronization. */
#ifndef RME_VULKAN_GPU_BUFFER_H
#define RME_VULKAN_GPU_BUFFER_H
#include "gpu_device.h"

enum rme_vk_buffer_status {
    RME_VK_BUFFER_OK = 0,
    RME_VK_BUFFER_INVALID_ARGUMENT,
    RME_VK_BUFFER_ALREADY_OPEN,
    RME_VK_BUFFER_FUNCTION_UNAVAILABLE,
    RME_VK_BUFFER_CREATE_FAILED,
    RME_VK_BUFFER_INVALID_REQUIREMENTS,
    RME_VK_BUFFER_LIMIT_EXCEEDED,
    RME_VK_BUFFER_NO_MEMORY_TYPE,
    RME_VK_BUFFER_ALLOCATE_FAILED,
    RME_VK_BUFFER_BIND_FAILED,
    RME_VK_BUFFER_INVALID_STATE,
    RME_VK_BUFFER_SUBMISSIONS_ACTIVE,
    RME_VK_BUFFER_DEVICE_UNAVAILABLE,
    RME_VK_BUFFER_COMPUTE_ACTIVE
};
struct rme_vk_gpu_buffer {
    enum rme_vk_buffer_status status;
    VkResult vk_result;
    VkDevice device; /* borrowed; GPU device must outlive buffer */
    struct rme_vk_gpu_device *owner;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size, allocation_size;
    VkBufferUsageFlags usage;
    uint32_t compute_references;
    uint32_t memory_type_index, memory_heap_index;
    VkMemoryPropertyFlags memory_properties;
    VkBool32 dedicated;
    PFN_vkDestroyBuffer destroy_buffer;
    PFN_vkFreeMemory free_memory;
};
/* State must be zero-initialized, exclusively owned, and not copied or cleared
 * while active. Caller serializes calls, keeps instance and GPU device alive,
 * and ensures no externally submitted work uses buffer before destroy. This
 * API refuses destruction while any tracked submission is active. No CPU mapping,
 * synchronization or data transfer is performed here. budget is a per-call
 * allocation limit, not a global memory budget. Usage accepts only
 * STORAGE_BUFFER, TRANSFER_SRC and TRANSFER_DST (at least one required).
 * gpu must be created by rme_vk_gpu_open for the supplied instance. Caller must
 * serialize all allocations on the device and account for every external
 * VkDeviceMemory allocation against maxMemoryAllocationCount; this component
 * can only count allocations created through this API. */
enum rme_vk_buffer_status rme_vk_gpu_buffer_create(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    VkDeviceSize size, VkDeviceSize budget, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
    struct rme_vk_gpu_buffer *state);
enum rme_vk_buffer_status rme_vk_gpu_buffer_destroy(struct rme_vk_gpu_buffer *state);
#endif
