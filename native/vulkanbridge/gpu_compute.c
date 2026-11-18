#include "gpu_compute.h"
#include <string.h>

static void release_objects(struct rme_vk_compute_dispatch *s) {
    VkDevice d=s->gpu->device;
    if(s->command_pool) s->destroy_command_pool(d,s->command_pool,NULL);
    if(s->pipeline) s->destroy_pipeline(d,s->pipeline,NULL);
    if(s->shader) s->destroy_shader(d,s->shader,NULL);
    if(s->pipeline_layout) s->destroy_pipeline_layout(d,s->pipeline_layout,NULL);
    if(s->descriptor_pool) s->destroy_descriptor_pool(d,s->descriptor_pool,NULL);
    if(s->descriptor_layout) s->destroy_descriptor_layout(d,s->descriptor_layout,NULL);
    --s->buffer->compute_references;
    s->command_pool=VK_NULL_HANDLE; s->pipeline=VK_NULL_HANDLE; s->shader=VK_NULL_HANDLE;
    s->pipeline_layout=VK_NULL_HANDLE; s->descriptor_pool=VK_NULL_HANDLE; s->descriptor_layout=VK_NULL_HANDLE;
    s->buffer=NULL; s->gpu=NULL;
}

enum rme_vk_compute_status rme_vk_compute_start(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    struct rme_vk_gpu_buffer *buffer, const uint32_t *spirv, size_t words,
    const char *entry, uint32_t x, uint32_t y, uint32_t z,
    struct rme_vk_compute_dispatch *s) {
    if(!s) return RME_VK_COMPUTE_INVALID_ARGUMENT;
    if(s->gpu || s->buffer) return s->status=RME_VK_COMPUTE_ALREADY_ACTIVE;
    memset(s,0,sizeof(*s));
    if(!instance || !get_proc || !gpu || !gpu->device || !gpu->queue || !gpu->selected.device ||
       !buffer || buffer->owner!=gpu || buffer->device!=gpu->device || !buffer->buffer || !buffer->memory ||
       !(buffer->usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || !buffer->size || !spirv || words<5 ||
       ((uintptr_t)spirv % sizeof(uint32_t)) || spirv[0]!=0x07230203 || !entry || !*entry || !x || !y || !z)
        return s->status=RME_VK_COMPUTE_INVALID_ARGUMENT;
    if(gpu->submission_unusable) { s->vk_result=gpu->submission_error; return s->status=RME_VK_COMPUTE_DEVICE_UNAVAILABLE; }
    const VkPhysicalDeviceLimits *lim=&gpu->selected.properties.limits;
    size_t entry_len=0; while(entry_len<256 && entry[entry_len]) ++entry_len;
    if(entry_len==256 || words>4194304 || buffer->size>lim->maxStorageBufferRange ||
       x>lim->maxComputeWorkGroupCount[0] || y>lim->maxComputeWorkGroupCount[1] || z>lim->maxComputeWorkGroupCount[2] ||
       !lim->maxPerStageDescriptorStorageBuffers || !lim->maxDescriptorSetStorageBuffers || !lim->maxBoundDescriptorSets ||
       !lim->maxPerStageResources || gpu->active_submissions || buffer->compute_references==UINT32_MAX || buffer->compute_references)
        return s->status=RME_VK_COMPUTE_LIMIT_EXCEEDED;
    PFN_vkGetDeviceProcAddr gdpa=(PFN_vkGetDeviceProcAddr)get_proc(instance,"vkGetDeviceProcAddr");
    if(!gdpa) return s->status=RME_VK_COMPUTE_FUNCTION_UNAVAILABLE;
#define GET(type,var,name) type var=(type)gdpa(gpu->device,name); if(!var) return s->status=RME_VK_COMPUTE_FUNCTION_UNAVAILABLE
    GET(PFN_vkCreateDescriptorSetLayout,create_dl,"vkCreateDescriptorSetLayout");
    GET(PFN_vkDestroyDescriptorSetLayout,destroy_dl,"vkDestroyDescriptorSetLayout");
    GET(PFN_vkCreateDescriptorPool,create_dp,"vkCreateDescriptorPool");
    GET(PFN_vkDestroyDescriptorPool,destroy_dp,"vkDestroyDescriptorPool");
    GET(PFN_vkAllocateDescriptorSets,allocate_ds,"vkAllocateDescriptorSets");
    GET(PFN_vkUpdateDescriptorSets,update_ds,"vkUpdateDescriptorSets");
    GET(PFN_vkCreatePipelineLayout,create_pl,"vkCreatePipelineLayout");
    GET(PFN_vkDestroyPipelineLayout,destroy_pl,"vkDestroyPipelineLayout");
    GET(PFN_vkCreateShaderModule,create_sm,"vkCreateShaderModule");
    GET(PFN_vkDestroyShaderModule,destroy_sm,"vkDestroyShaderModule");
    GET(PFN_vkCreateComputePipelines,create_cp,"vkCreateComputePipelines");
    GET(PFN_vkDestroyPipeline,destroy_p,"vkDestroyPipeline");
    GET(PFN_vkCreateCommandPool,create_pool,"vkCreateCommandPool");
    GET(PFN_vkDestroyCommandPool,destroy_pool,"vkDestroyCommandPool");
    GET(PFN_vkAllocateCommandBuffers,allocate_cb,"vkAllocateCommandBuffers");
    GET(PFN_vkBeginCommandBuffer,begin_cb,"vkBeginCommandBuffer");
    GET(PFN_vkEndCommandBuffer,end_cb,"vkEndCommandBuffer");
    GET(PFN_vkCmdPipelineBarrier,barrier,"vkCmdPipelineBarrier");
    GET(PFN_vkCmdBindPipeline,bind_pipeline,"vkCmdBindPipeline");
    GET(PFN_vkCmdBindDescriptorSets,bind_sets,"vkCmdBindDescriptorSets");
    GET(PFN_vkCmdDispatch,dispatch,"vkCmdDispatch");
#undef GET
    s->gpu=gpu; s->buffer=buffer; ++buffer->compute_references;
    s->destroy_descriptor_layout=destroy_dl; s->destroy_descriptor_pool=destroy_dp; s->destroy_pipeline_layout=destroy_pl;
    s->destroy_shader=destroy_sm; s->destroy_pipeline=destroy_p; s->destroy_command_pool=destroy_pool;
#define CREATE(call,member) do { s->vk_result=(call); if(s->vk_result!=VK_SUCCESS || !s->member) { s->member=VK_NULL_HANDLE; goto fail; } } while(0)
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo dli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    CREATE(create_dl(gpu->device,&dli,NULL,&s->descriptor_layout),descriptor_layout);
    VkDescriptorPoolSize ps={.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1};
    VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    CREATE(create_dp(gpu->device,&dpi,NULL,&s->descriptor_pool),descriptor_pool);
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=s->descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&s->descriptor_layout};
    VkDescriptorSet ds=VK_NULL_HANDLE;
    s->vk_result=allocate_ds(gpu->device,&dai,&ds); if(s->vk_result!=VK_SUCCESS || !ds) goto fail;
    VkDescriptorBufferInfo bi={.buffer=buffer->buffer,.offset=0,.range=buffer->size};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ds,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};
    update_ds(gpu->device,1,&write,0,NULL);
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&s->descriptor_layout};
    CREATE(create_pl(gpu->device,&pli,NULL,&s->pipeline_layout),pipeline_layout);
    VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*sizeof(uint32_t),.pCode=spirv};
    CREATE(create_sm(gpu->device,&smi,NULL,&s->shader),shader);
    VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=s->shader,.pName=entry};
    VkComputePipelineCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage=stage,.layout=s->pipeline_layout};
    s->vk_result=create_cp(gpu->device,VK_NULL_HANDLE,1,&cpi,NULL,&s->pipeline);
    if(s->vk_result!=VK_SUCCESS || !s->pipeline) goto fail; /* A failed call can return a pipeline. */
    VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=gpu->selected.queue_family};
    CREATE(create_pool(gpu->device,&pci,NULL,&s->command_pool),command_pool);
#undef CREATE
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=s->command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command=VK_NULL_HANDLE;
    s->vk_result=allocate_cb(gpu->device,&cai,&command); if(s->vk_result!=VK_SUCCESS || !command) goto fail;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    s->vk_result=begin_cb(command,&begin); if(s->vk_result!=VK_SUCCESS) goto fail;
    VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
    barrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
    bind_pipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline);
    bind_sets(command,VK_PIPELINE_BIND_POINT_COMPUTE,s->pipeline_layout,0,1,&ds,0,NULL);
    dispatch(command,x,y,z);
    VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    barrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
    s->vk_result=end_cb(command); if(s->vk_result!=VK_SUCCESS) goto fail;
    if(rme_vk_gpu_submit(instance,get_proc,gpu,command,&s->submission)!=RME_VK_SUBMISSION_OK) {
        s->vk_result=s->submission.vk_result;
        if(s->submission.phase==RME_VK_SUBMISSION_EMPTY) release_objects(s);
        return s->status=RME_VK_COMPUTE_SUBMIT_ERROR;
    }
    s->vk_result=s->submission.vk_result;
    return s->status=RME_VK_COMPUTE_OK;
fail:
    if(s->vk_result==VK_ERROR_DEVICE_LOST) { gpu->submission_unusable=VK_TRUE; gpu->submission_error=VK_ERROR_DEVICE_LOST; }
    release_objects(s); return s->status=RME_VK_COMPUTE_VULKAN_ERROR;
}

enum rme_vk_compute_status rme_vk_compute_wait(struct rme_vk_compute_dispatch *s,uint64_t timeout_ns) {
    if(!s || !s->gpu || !s->buffer) return RME_VK_COMPUTE_INVALID_STATE;
    enum rme_vk_submission_status r=rme_vk_gpu_submission_wait(&s->submission,timeout_ns);
    s->vk_result=s->submission.vk_result;
    if(r==RME_VK_SUBMISSION_OK) return s->status=RME_VK_COMPUTE_OK;
    if(r==RME_VK_SUBMISSION_TIMEOUT) return s->status=RME_VK_COMPUTE_TIMEOUT;
    if(r==RME_VK_SUBMISSION_OUTCOME_UNKNOWN) return s->status=RME_VK_COMPUTE_OUTCOME_UNKNOWN;
    return s->status=RME_VK_COMPUTE_WAIT_ERROR;
}

enum rme_vk_compute_status rme_vk_compute_dispose(struct rme_vk_compute_dispatch *s) {
    if(!s) return RME_VK_COMPUTE_INVALID_ARGUMENT;
    if(!s->gpu && !s->buffer) return RME_VK_COMPUTE_OK;
    if(!s->gpu || !s->buffer || s->buffer->owner!=s->gpu || !s->buffer->compute_references) return s->status=RME_VK_COMPUTE_INVALID_STATE;
    enum rme_vk_submission_status r=rme_vk_gpu_submission_dispose(&s->submission);
    if(r==RME_VK_SUBMISSION_STILL_PENDING) return s->status=RME_VK_COMPUTE_STILL_PENDING;
    if(r==RME_VK_SUBMISSION_OUTCOME_UNKNOWN) return s->status=RME_VK_COMPUTE_OUTCOME_UNKNOWN;
    if(r!=RME_VK_SUBMISSION_OK) return s->status=RME_VK_COMPUTE_INVALID_STATE;
    release_objects(s); memset(s,0,sizeof(*s)); return RME_VK_COMPUTE_OK;
}
