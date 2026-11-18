#include "gpu_device.h"
#include <string.h>

enum rme_vk_device_status rme_vk_gpu_open(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *state) {
    return rme_vk_gpu_open_with_features(instance, get_proc, NULL, state);
}

enum rme_vk_device_status rme_vk_gpu_open_with_features(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, const VkPhysicalDeviceFeatures *requested,
    struct rme_vk_gpu_device *state) {
    if (!state) return RME_VK_DEVICE_INVALID_ARGUMENT;
    if (state->device) return state->status = RME_VK_DEVICE_ALREADY_OPEN;
    VkPhysicalDeviceFeatures request = {0};
    if (requested) request = *requested; /* Caller may point inside state. */
    memset(state, 0, sizeof(*state));
    if (!instance || !get_proc) return state->status = RME_VK_DEVICE_INVALID_ARGUMENT;
    state->select_status = rme_vk_select_gpu(instance, get_proc, &state->selected);
    state->vk_result = state->selected.vk_result;
    if (state->select_status != RME_VK_SELECT_OK) return state->status = RME_VK_DEVICE_SELECT_FAILED;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)get_proc(instance, "vkCreateDevice");
    PFN_vkGetDeviceQueue get_queue = (PFN_vkGetDeviceQueue)get_proc(instance, "vkGetDeviceQueue");
    PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)get_proc(instance, "vkDestroyDevice");
    if (!create || !get_queue || !destroy) return state->status = RME_VK_DEVICE_FUNCTION_UNAVAILABLE;
    VkPhysicalDeviceFeatures enabled = {0};
    if (requested) {
        PFN_vkGetPhysicalDeviceFeatures get_features =
            (PFN_vkGetPhysicalDeviceFeatures)get_proc(instance, "vkGetPhysicalDeviceFeatures");
        if (!get_features) return state->status = RME_VK_DEVICE_FUNCTION_UNAVAILABLE;
        VkPhysicalDeviceFeatures supported = {0};
        get_features(state->selected.device, &supported); /* Returns no VkResult. */
        /* Vulkan 1.0 has a fixed list of independently named VkBool32 members.
         * Copy fieldwise: this neither assumes struct packing nor aliases it as
         * an array. Noncanonical nonzero requests become Vulkan VK_TRUE. */
#define FEATURE(name) do { if (request.name) { \
    if (!supported.name) return state->status = RME_VK_DEVICE_FEATURE_UNSUPPORTED; \
    enabled.name = VK_TRUE; \
} } while (0)
        FEATURE(robustBufferAccess);
        FEATURE(fullDrawIndexUint32);
        FEATURE(imageCubeArray);
        FEATURE(independentBlend);
        FEATURE(geometryShader);
        FEATURE(tessellationShader);
        FEATURE(sampleRateShading);
        FEATURE(dualSrcBlend);
        FEATURE(logicOp);
        FEATURE(multiDrawIndirect);
        FEATURE(drawIndirectFirstInstance);
        FEATURE(depthClamp);
        FEATURE(depthBiasClamp);
        FEATURE(fillModeNonSolid);
        FEATURE(depthBounds);
        FEATURE(wideLines);
        FEATURE(largePoints);
        FEATURE(alphaToOne);
        FEATURE(multiViewport);
        FEATURE(samplerAnisotropy);
        FEATURE(textureCompressionETC2);
        FEATURE(textureCompressionASTC_LDR);
        FEATURE(textureCompressionBC);
        FEATURE(occlusionQueryPrecise);
        FEATURE(pipelineStatisticsQuery);
        FEATURE(vertexPipelineStoresAndAtomics);
        FEATURE(fragmentStoresAndAtomics);
        FEATURE(shaderTessellationAndGeometryPointSize);
        FEATURE(shaderImageGatherExtended);
        FEATURE(shaderStorageImageExtendedFormats);
        FEATURE(shaderStorageImageMultisample);
        FEATURE(shaderStorageImageReadWithoutFormat);
        FEATURE(shaderStorageImageWriteWithoutFormat);
        FEATURE(shaderUniformBufferArrayDynamicIndexing);
        FEATURE(shaderSampledImageArrayDynamicIndexing);
        FEATURE(shaderStorageBufferArrayDynamicIndexing);
        FEATURE(shaderStorageImageArrayDynamicIndexing);
        FEATURE(shaderClipDistance);
        FEATURE(shaderCullDistance);
        FEATURE(shaderFloat64);
        FEATURE(shaderInt64);
        FEATURE(shaderInt16);
        FEATURE(shaderResourceResidency);
        FEATURE(shaderResourceMinLod);
        FEATURE(sparseBinding);
        FEATURE(sparseResidencyBuffer);
        FEATURE(sparseResidencyImage2D);
        FEATURE(sparseResidencyImage3D);
        FEATURE(sparseResidency2Samples);
        FEATURE(sparseResidency4Samples);
        FEATURE(sparseResidency8Samples);
        FEATURE(sparseResidency16Samples);
        FEATURE(sparseResidencyAliased);
        FEATURE(variableMultisampleRate);
        FEATURE(inheritedQueries);
#undef FEATURE
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=state->selected.queue_family, .queueCount=1, .pQueuePriorities=&priority };
    VkDeviceCreateInfo ci = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1, .pQueueCreateInfos=&qi,
        .pEnabledFeatures=requested ? &enabled : NULL };
    VkDevice device = VK_NULL_HANDLE;
    state->vk_result = create(state->selected.device, &ci, NULL, &device);
    if (state->vk_result != VK_SUCCESS || !device) return state->status = RME_VK_DEVICE_CREATE_FAILED;
    VkQueue queue = VK_NULL_HANDLE;
    get_queue(device, state->selected.queue_family, 0, &queue);
    if (!queue) {
        destroy(device, NULL);
        return state->status = RME_VK_DEVICE_QUEUE_UNAVAILABLE;
    }
    state->device=device; state->queue=queue; state->destroy=destroy;
    state->enabled_features=enabled;
    return state->status = RME_VK_DEVICE_OK;
}

const VkPhysicalDeviceFeatures *rme_vk_gpu_enabled_features(const struct rme_vk_gpu_device *state) {
    return state && state->device ? &state->enabled_features : NULL;
}

enum rme_vk_device_status rme_vk_gpu_close(struct rme_vk_gpu_device *state) {
    if (!state) return RME_VK_DEVICE_INVALID_ARGUMENT;
    if ((state->active_buffer_allocations || state->active_submissions) && !state->device) return state->status = RME_VK_DEVICE_INVALID_STATE;
    if (state->active_buffer_allocations) return state->status = RME_VK_DEVICE_BUFFERS_ACTIVE;
    if (state->active_submissions) return state->status = RME_VK_DEVICE_SUBMISSIONS_ACTIVE;
    if (state->device && !state->destroy) return state->status = RME_VK_DEVICE_INVALID_STATE;
    if (state->device) state->destroy(state->device, NULL);
    memset(state, 0, sizeof(*state));
    return RME_VK_DEVICE_OK;
}
