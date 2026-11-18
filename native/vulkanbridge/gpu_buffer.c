#include "gpu_buffer.h"
#include <string.h>

enum rme_vk_buffer_status rme_vk_gpu_buffer_create(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    VkDeviceSize size, VkDeviceSize budget, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
    struct rme_vk_gpu_buffer *state) {
    if (!state) return RME_VK_BUFFER_INVALID_ARGUMENT;
    if (state->buffer || state->memory) return state->status=RME_VK_BUFFER_ALREADY_OPEN;
    memset(state,0,sizeof(*state));
    const VkBufferUsageFlags supported=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags unavailable=VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
#ifdef VK_AMD_device_coherent_memory
    /* gpu_device does not enable deviceCoherentMemory. */
    unavailable |= VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD|VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;
#endif
    if (!instance || !get_proc || !gpu || !gpu->device || !gpu->queue || !gpu->selected.device ||
        gpu->selected.status!=RME_VK_SELECT_OK ||
        (gpu->selected.properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
         gpu->selected.properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ||
        VK_API_VERSION_VARIANT(gpu->selected.properties.apiVersion)!=0 ||
        gpu->selected.properties.apiVersion<VK_API_VERSION_1_2 ||
        !size || !budget || !usage || (usage & ~supported) || (required & unavailable))
        return state->status=RME_VK_BUFFER_INVALID_ARGUMENT;
    if (gpu->submission_unusable) { state->vk_result=gpu->submission_error; return state->status=RME_VK_BUFFER_DEVICE_UNAVAILABLE; }
    if (size>budget) return state->status=RME_VK_BUFFER_LIMIT_EXCEEDED;
    uint32_t limit=gpu->selected.properties.limits.maxMemoryAllocationCount;
    if (!limit) return state->status=RME_VK_BUFFER_INVALID_REQUIREMENTS;
    if (gpu->active_buffer_allocations>=limit) return state->status=RME_VK_BUFFER_LIMIT_EXCEEDED;
    PFN_vkGetDeviceProcAddr gdpa=(PFN_vkGetDeviceProcAddr)get_proc(instance,"vkGetDeviceProcAddr");
    PFN_vkGetPhysicalDeviceMemoryProperties get_properties=(PFN_vkGetPhysicalDeviceMemoryProperties)get_proc(instance,"vkGetPhysicalDeviceMemoryProperties");
    if (!gdpa || !get_properties) return state->status=RME_VK_BUFFER_FUNCTION_UNAVAILABLE;
    VkDevice device=gpu->device;
    PFN_vkCreateBuffer create=(PFN_vkCreateBuffer)gdpa(device,"vkCreateBuffer");
    PFN_vkDestroyBuffer destroy=(PFN_vkDestroyBuffer)gdpa(device,"vkDestroyBuffer");
    PFN_vkGetBufferMemoryRequirements2 get_req=(PFN_vkGetBufferMemoryRequirements2)gdpa(device,"vkGetBufferMemoryRequirements2");
    PFN_vkAllocateMemory allocate=(PFN_vkAllocateMemory)gdpa(device,"vkAllocateMemory");
    PFN_vkFreeMemory free_memory=(PFN_vkFreeMemory)gdpa(device,"vkFreeMemory");
    PFN_vkBindBufferMemory bind=(PFN_vkBindBufferMemory)gdpa(device,"vkBindBufferMemory");
    if (!create || !destroy || !get_req || !allocate || !free_memory || !bind)
        return state->status=RME_VK_BUFFER_FUNCTION_UNAVAILABLE;
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    state->vk_result=create(device,&ci,NULL,&buffer);
    if (state->vk_result!=VK_SUCCESS || !buffer) return state->status=RME_VK_BUFFER_CREATE_FAILED;
    VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 req={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
    VkBufferMemoryRequirementsInfo2 ri={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=buffer};
    get_req(device,&ri,&req); /* Vulkan returns no VkResult. */
    VkMemoryRequirements r=req.memoryRequirements;
    if (r.size<size || !r.alignment || (r.alignment&(r.alignment-1)) || !r.memoryTypeBits ||
        (dedicated.requiresDedicatedAllocation!=VK_FALSE && dedicated.requiresDedicatedAllocation!=VK_TRUE)) {
        state->status=RME_VK_BUFFER_INVALID_REQUIREMENTS; goto cleanup;
    }
    if (r.size>budget) { state->status=RME_VK_BUFFER_LIMIT_EXCEEDED; goto cleanup; }
    VkPhysicalDeviceMemoryProperties mp; memset(&mp,0,sizeof(mp));
    get_properties(gpu->selected.device,&mp); /* Vulkan returns no VkResult. */
    if (!mp.memoryTypeCount || mp.memoryTypeCount>VK_MAX_MEMORY_TYPES ||
        !mp.memoryHeapCount || mp.memoryHeapCount>VK_MAX_MEMORY_HEAPS) {
        state->status=RME_VK_BUFFER_INVALID_REQUIREMENTS; goto cleanup;
    }
    for (uint32_t i=0;i<mp.memoryTypeCount;++i) if (mp.memoryTypes[i].heapIndex>=mp.memoryHeapCount) {
        state->status=RME_VK_BUFFER_INVALID_REQUIREMENTS; goto cleanup;
    }
    uint32_t index=UINT32_MAX;
    for (uint32_t i=0;i<mp.memoryTypeCount;++i) {
        VkMemoryType t=mp.memoryTypes[i];
        if ((r.memoryTypeBits&(1u<<i)) && (t.propertyFlags&required)==required &&
            !(t.propertyFlags&unavailable) && r.size<=mp.memoryHeaps[t.heapIndex].size) { index=i; break; }
    }
    if (index==UINT32_MAX) { state->status=RME_VK_BUFFER_NO_MEMORY_TYPE; goto cleanup; }
    VkMemoryDedicatedAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=buffer};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext=dedicated.requiresDedicatedAllocation ? &dai : NULL,.allocationSize=r.size,.memoryTypeIndex=index};
    VkDeviceMemory allocated=VK_NULL_HANDLE;
    state->vk_result=allocate(device,&ai,NULL,&allocated);
    if (state->vk_result!=VK_SUCCESS || !allocated) { state->status=RME_VK_BUFFER_ALLOCATE_FAILED; goto cleanup; }
    memory=allocated;
    ++gpu->active_buffer_allocations;
    state->vk_result=bind(device,buffer,memory,0);
    if (state->vk_result!=VK_SUCCESS) { state->status=RME_VK_BUFFER_BIND_FAILED; goto cleanup; }
    state->device=device; state->owner=gpu; state->buffer=buffer; state->memory=memory; state->size=size; state->allocation_size=r.size;
    state->memory_type_index=index; state->memory_heap_index=mp.memoryTypes[index].heapIndex;
    state->memory_properties=mp.memoryTypes[index].propertyFlags; state->dedicated=dedicated.requiresDedicatedAllocation; state->usage=usage;
    state->destroy_buffer=destroy; state->free_memory=free_memory;
    return state->status=RME_VK_BUFFER_OK;
cleanup:
    destroy(device,buffer,NULL);
    if (memory) { free_memory(device,memory,NULL); --gpu->active_buffer_allocations; }
    return state->status;
}

enum rme_vk_buffer_status rme_vk_gpu_buffer_destroy(struct rme_vk_gpu_buffer *state) {
    if (!state) return RME_VK_BUFFER_INVALID_ARGUMENT;
    if ((state->buffer || state->memory) && (!state->buffer || !state->memory || !state->device ||
        !state->owner || state->owner->device!=state->device || !state->owner->active_buffer_allocations ||
        !state->destroy_buffer || !state->free_memory))
        return state->status=RME_VK_BUFFER_INVALID_STATE;
    if (state->buffer && state->compute_references) return state->status=RME_VK_BUFFER_COMPUTE_ACTIVE;
    if (state->buffer && state->owner->active_submissions) return state->status=RME_VK_BUFFER_SUBMISSIONS_ACTIVE;
    if (state->buffer) state->destroy_buffer(state->device,state->buffer,NULL);
    if (state->memory) { state->free_memory(state->device,state->memory,NULL); --state->owner->active_buffer_allocations; }
    memset(state,0,sizeof(*state)); return RME_VK_BUFFER_OK;
}
