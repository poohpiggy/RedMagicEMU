/* A single built-in, validated shader. It is not a general Guest Vulkan API. */
#include "host_fixed_compute.h"
#include "gpu_buffer.h"
#include "gpu_compute.h"
#include "host_shader_spv.h"
#include <string.h>

enum rme_vk_fixed_status rme_vk_host_fixed_compute(VkInstance instance,
    struct rme_vk_gpu_device *gpu, const uint32_t input[RME_VK_FIXED_WORDS],
    uint32_t output[RME_VK_FIXED_WORDS]) {
    if (!instance || !gpu || !gpu->device || !gpu->queue || !input || !output)
        return RME_VK_FIXED_FAILED;
    const VkPhysicalDeviceLimits *limits = &gpu->selected.properties.limits;
    if (limits->maxComputeWorkGroupSize[0] < 16 || !limits->maxComputeWorkGroupSize[1] ||
        !limits->maxComputeWorkGroupSize[2] || limits->maxComputeWorkGroupInvocations < 16)
        return RME_VK_FIXED_FAILED;

    struct rme_vk_gpu_buffer buffer = {0};
    struct rme_vk_compute_dispatch compute = {0};
    enum rme_vk_fixed_status result = RME_VK_FIXED_FAILED;
    int mapped = 0, buffer_created = 0;
    void *address = NULL;
    uint32_t verified[RME_VK_FIXED_WORDS];
    const VkDeviceSize bytes = RME_VK_FIXED_WORDS * sizeof(uint32_t);
    if (rme_vk_gpu_buffer_create(instance, vkGetInstanceProcAddr, gpu, bytes, 4194304,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &buffer) != RME_VK_BUFFER_OK)
        goto cleanup;
    buffer_created = 1;
    if (vkMapMemory(gpu->device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &address) != VK_SUCCESS)
        goto cleanup;
    mapped = 1;
    if (!address) goto cleanup;
    memcpy(address, input, (size_t)bytes);
    const int coherent = !!(buffer.memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMappedMemoryRange range = { .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory=buffer.memory, .offset=0, .size=VK_WHOLE_SIZE };
    if (!coherent && vkFlushMappedMemoryRanges(gpu->device, 1, &range) != VK_SUCCESS) goto cleanup;
    if (rme_vk_compute_start(instance, vkGetInstanceProcAddr, gpu, &buffer,
            host_shader_spv, sizeof(host_shader_spv)/sizeof(host_shader_spv[0]),
            "main", 4, 1, 1, &compute) != RME_VK_COMPUTE_OK) goto cleanup;
    if (rme_vk_compute_wait(&compute, 5000000000ULL) != RME_VK_COMPUTE_OK) goto cleanup;
    if (rme_vk_compute_dispose(&compute) != RME_VK_COMPUTE_OK) return RME_VK_FIXED_UNRESOLVED;
    if (!coherent && vkInvalidateMappedMemoryRanges(gpu->device, 1, &range) != VK_SUCCESS) goto cleanup;
    const uint32_t *values = address;
    for (unsigned i=0; i<RME_VK_FIXED_WORDS; ++i)
        if (values[i] != input[i] * 3u + 7u) goto cleanup;
    memcpy(verified, values, (size_t)bytes);
    result = RME_VK_FIXED_OK;
cleanup:
    /* Pending or unknown work can still reference all resources: retain them. */
    if (rme_vk_compute_dispose(&compute) != RME_VK_COMPUTE_OK) return RME_VK_FIXED_UNRESOLVED;
    if (mapped) vkUnmapMemory(gpu->device, buffer.memory);
    if (buffer_created && rme_vk_gpu_buffer_destroy(&buffer) != RME_VK_BUFFER_OK)
        return RME_VK_FIXED_UNRESOLVED;
    if (result == RME_VK_FIXED_OK) memcpy(output, verified, (size_t)bytes);
    return result;
}
