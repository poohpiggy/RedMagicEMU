/* Fake Vulkan dispatch, lifecycle tests only; this is not GPU execution. */
#include "../gpu_compute.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned step, fail_step, created, destroyed, submits, waits;
static unsigned barriers, dispatches, group_x, group_y, group_z;
static const char *missing;
static VkResult wait_result=VK_SUCCESS, submit_result=VK_SUCCESS;
static VkResult stage_failure=VK_ERROR_OUT_OF_HOST_MEMORY;
static int dirty_pipeline;
static const VkInstance instance=(VkInstance)(uintptr_t)1;
static const VkPhysicalDevice physical=(VkPhysicalDevice)(uintptr_t)2;
static const VkDevice device=(VkDevice)(uintptr_t)3;
static const VkQueue queue=(VkQueue)(uintptr_t)4;
static const VkBuffer raw_buffer=(VkBuffer)(uintptr_t)5;
static const VkDeviceMemory memory=(VkDeviceMemory)(uintptr_t)6;
static VkResult next(void) { return ++step==fail_step ? stage_failure : VK_SUCCESS; }
#define CREATE(name, type, infotype) \
static VKAPI_ATTR VkResult VKAPI_CALL name(VkDevice d,const infotype *i,const VkAllocationCallbacks *a,type *out) { \
    (void)d;(void)i;(void)a; VkResult r=next(); if(r==VK_SUCCESS) { *out=(type)(uintptr_t)(100+created); ++created; } return r; }
#define DESTROY(name, type) \
static VKAPI_ATTR void VKAPI_CALL name(VkDevice d,type obj,const VkAllocationCallbacks *a) { (void)d;(void)a; if(obj) ++destroyed; }
CREATE(create_layout,VkDescriptorSetLayout,VkDescriptorSetLayoutCreateInfo)
CREATE(create_pool,VkDescriptorPool,VkDescriptorPoolCreateInfo)
CREATE(create_pipeline_layout,VkPipelineLayout,VkPipelineLayoutCreateInfo)
CREATE(create_shader,VkShaderModule,VkShaderModuleCreateInfo)
CREATE(create_command_pool,VkCommandPool,VkCommandPoolCreateInfo)
DESTROY(destroy_layout,VkDescriptorSetLayout)
DESTROY(destroy_pool,VkDescriptorPool)
DESTROY(destroy_pipeline_layout,VkPipelineLayout)
DESTROY(destroy_shader,VkShaderModule)
DESTROY(destroy_pipeline,VkPipeline)
DESTROY(destroy_command_pool,VkCommandPool)
static VKAPI_ATTR VkResult VKAPI_CALL allocate_set(VkDevice d,const VkDescriptorSetAllocateInfo *i,VkDescriptorSet *out) { (void)d;(void)i; VkResult r=next(); if(r==VK_SUCCESS) *out=(VkDescriptorSet)(uintptr_t)9; return r; }
static VKAPI_ATTR void VKAPI_CALL update(VkDevice d,uint32_t n,const VkWriteDescriptorSet *w,uint32_t c,const VkCopyDescriptorSet *p) { (void)d;(void)n;(void)w;(void)c;(void)p; }
static VKAPI_ATTR VkResult VKAPI_CALL create_pipeline(VkDevice d,VkPipelineCache cache,uint32_t n,const VkComputePipelineCreateInfo *i,const VkAllocationCallbacks *a,VkPipeline *out) {
    (void)d;(void)cache;(void)n;(void)i;(void)a; VkResult r=next();
    if(r==VK_SUCCESS || dirty_pipeline) { *out=(VkPipeline)(uintptr_t)(100+created); ++created; } return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL allocate_command(VkDevice d,const VkCommandBufferAllocateInfo *i,VkCommandBuffer *out) { (void)d;(void)i; VkResult r=next(); if(r==VK_SUCCESS) *out=(VkCommandBuffer)(uintptr_t)10; return r; }
static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer c,const VkCommandBufferBeginInfo *i) { (void)c;(void)i; return next(); }
static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer c) { (void)c; return next(); }
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer c,VkPipelineStageFlags a,VkPipelineStageFlags b,VkDependencyFlags f,uint32_t n,const VkMemoryBarrier *m,uint32_t bc,const VkBufferMemoryBarrier *bm,uint32_t ic,const VkImageMemoryBarrier *im) { (void)c;(void)a;(void)b;(void)f;(void)n;(void)m;(void)bc;(void)bm;(void)ic;(void)im; ++barriers; }
static VKAPI_ATTR void VKAPI_CALL bind_pipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline o) { (void)c;(void)p;(void)o; }
static VKAPI_ATTR void VKAPI_CALL bind_sets(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,uint32_t f,uint32_t n,const VkDescriptorSet *s,uint32_t dc,const uint32_t *offs) { (void)c;(void)p;(void)l;(void)f;(void)n;(void)s;(void)dc;(void)offs; }
static VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z) { (void)c; ++dispatches; group_x=x; group_y=y; group_z=z; }
static VKAPI_ATTR VkResult VKAPI_CALL create_fence(VkDevice d,const VkFenceCreateInfo *i,const VkAllocationCallbacks *a,VkFence *out) { (void)d;(void)i;(void)a; VkResult r=next(); if(r==VK_SUCCESS) { *out=(VkFence)(uintptr_t)11; ++created; } return r; }
static VKAPI_ATTR void VKAPI_CALL destroy_fence(VkDevice d,VkFence f,const VkAllocationCallbacks *a) { (void)d;(void)a; if(f) ++destroyed; }
static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue q,uint32_t n,const VkSubmitInfo *i,VkFence f) { (void)q;(void)n;(void)i;(void)f; ++submits; return submit_result; }
static VKAPI_ATTR VkResult VKAPI_CALL wait_fences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t t) { (void)d;(void)n;(void)f;(void)all;(void)t; ++waits; return wait_result; }
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d,const VkAllocationCallbacks *a) { (void)d;(void)a; }
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks *a) { (void)d;(void)b;(void)a; }
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a) { (void)d;(void)m;(void)a; }
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice d,const char *name) {
    (void)d;
    if(missing && !strcmp(name,missing)) return NULL;
#define MAP(v,n) if(!strcmp(name,v)) return (PFN_vkVoidFunction)n
    MAP("vkCreateDescriptorSetLayout",create_layout); MAP("vkDestroyDescriptorSetLayout",destroy_layout);
    MAP("vkCreateDescriptorPool",create_pool); MAP("vkDestroyDescriptorPool",destroy_pool);
    MAP("vkAllocateDescriptorSets",allocate_set); MAP("vkUpdateDescriptorSets",update);
    MAP("vkCreatePipelineLayout",create_pipeline_layout); MAP("vkDestroyPipelineLayout",destroy_pipeline_layout);
    MAP("vkCreateShaderModule",create_shader); MAP("vkDestroyShaderModule",destroy_shader);
    MAP("vkCreateComputePipelines",create_pipeline); MAP("vkDestroyPipeline",destroy_pipeline);
    MAP("vkCreateCommandPool",create_command_pool); MAP("vkDestroyCommandPool",destroy_command_pool);
    MAP("vkAllocateCommandBuffers",allocate_command); MAP("vkBeginCommandBuffer",begin); MAP("vkEndCommandBuffer",end);
    MAP("vkCmdPipelineBarrier",barrier); MAP("vkCmdBindPipeline",bind_pipeline); MAP("vkCmdBindDescriptorSets",bind_sets); MAP("vkCmdDispatch",dispatch);
    MAP("vkCreateFence",create_fence); MAP("vkDestroyFence",destroy_fence); MAP("vkQueueSubmit",submit); MAP("vkWaitForFences",wait_fences);
#undef MAP
    return NULL;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i,const char *name) { (void)i; return !strcmp(name,"vkGetDeviceProcAddr")?(PFN_vkVoidFunction)gdpa:NULL; }
static void reset(struct rme_vk_gpu_device *g,struct rme_vk_gpu_buffer *b,struct rme_vk_compute_dispatch *s) {
    memset(g,0,sizeof(*g)); memset(b,0,sizeof(*b)); memset(s,0,sizeof(*s));
    step=fail_step=created=destroyed=submits=waits=0; barriers=dispatches=group_x=group_y=group_z=0;
    dirty_pipeline=0; missing=NULL; wait_result=submit_result=VK_SUCCESS; stage_failure=VK_ERROR_OUT_OF_HOST_MEMORY;
    g->device=device; g->queue=queue; g->destroy=destroy_device; g->selected.device=physical; g->active_buffer_allocations=1;
    g->selected.properties.limits.maxStorageBufferRange=65536; g->selected.properties.limits.maxComputeWorkGroupCount[0]=64;
    g->selected.properties.limits.maxComputeWorkGroupCount[1]=64; g->selected.properties.limits.maxComputeWorkGroupCount[2]=64;
    g->selected.properties.limits.maxPerStageDescriptorStorageBuffers=1; g->selected.properties.limits.maxDescriptorSetStorageBuffers=1;
    g->selected.properties.limits.maxBoundDescriptorSets=1; g->selected.properties.limits.maxPerStageResources=1;
    b->device=device; b->owner=g; b->buffer=raw_buffer; b->memory=memory; b->size=256; b->usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    b->destroy_buffer=destroy_buffer; b->free_memory=free_memory;
}
static const uint32_t shader[]={0x07230203,0x00010000,0,1,0}; /* fake driver does not validate SPIR-V */
static int fail(const char *s) { fprintf(stderr,"FAIL %s step=%u created=%u destroyed=%u\n",s,step,created,destroyed); return 1; }
#define START(g,b,s,x) rme_vk_compute_start(instance,get_proc,&(g),&(b),shader,5,"main",(x),1,1,&(s))
int main(void) {
    struct rme_vk_gpu_device g; struct rme_vk_gpu_buffer b; struct rme_vk_compute_dispatch s;
    reset(&g,&b,&s);
    if(START(g,b,s,0)!=RME_VK_COMPUTE_INVALID_ARGUMENT || created) return fail("zero groups");
    if(START(g,b,s,65)!=RME_VK_COMPUTE_LIMIT_EXCEEDED || created) return fail("group limit");
    b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_INVALID_ARGUMENT || created) return fail("storage usage");
    reset(&g,&b,&s); b.size=65537;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_LIMIT_EXCEEDED || created) return fail("storage range");
    reset(&g,&b,&s); g.selected.properties.limits.maxPerStageDescriptorStorageBuffers=0;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_LIMIT_EXCEEDED || created) return fail("descriptor limit");
    reset(&g,&b,&s); g.submission_unusable=VK_TRUE; g.submission_error=VK_ERROR_DEVICE_LOST;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_DEVICE_UNAVAILABLE || s.vk_result!=VK_ERROR_DEVICE_LOST || created || step) return fail("unusable device root error");
    reset(&g,&b,&s);
    if(rme_vk_compute_start(instance,get_proc,&g,&b,shader,4,"main",1,1,1,&s)!=RME_VK_COMPUTE_INVALID_ARGUMENT ||
       rme_vk_compute_start(instance,get_proc,&g,&b,shader,5,"",1,1,1,&s)!=RME_VK_COMPUTE_INVALID_ARGUMENT || created) return fail("shader inputs");
    const uint32_t bad_shader[]={0,0,0,0,0};
    if(rme_vk_compute_start(instance,get_proc,&g,&b,bad_shader,5,"main",1,1,1,&s)!=RME_VK_COMPUTE_INVALID_ARGUMENT || created) return fail("shader magic");
    const char *names[]={"vkCreateDescriptorSetLayout","vkDestroyDescriptorPool","vkCreateComputePipelines","vkCmdDispatch"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        reset(&g,&b,&s); missing=names[i];
        if(START(g,b,s,1)!=RME_VK_COMPUTE_FUNCTION_UNAVAILABLE || created || b.compute_references) return fail("missing entrypoint");
    }
    for(unsigned n=1;n<=11;n++) {
        reset(&g,&b,&s); fail_step=n; dirty_pipeline=1;
        if(START(g,b,s,1)==RME_VK_COMPUTE_OK || created!=destroyed || b.compute_references || g.active_submissions) return fail("stage failure cleanup");
    }
    /* DEVICE_LOST is not a listed vkCreateComputePipelines result in the
     * fixed registry; this is a defensive nonconforming-driver case. */
    reset(&g,&b,&s); fail_step=6; stage_failure=VK_ERROR_DEVICE_LOST; dirty_pipeline=1;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_VULKAN_ERROR || s.vk_result!=VK_ERROR_DEVICE_LOST ||
       !g.submission_unusable || g.submission_error!=VK_ERROR_DEVICE_LOST || created!=destroyed || b.compute_references) return fail("pipeline loss blocks device");
    reset(&g,&b,&s);
    if(START(g,b,s,2)!=RME_VK_COMPUTE_OK || b.compute_references!=1 || g.active_submissions!=1 ||
       barriers!=2 || dispatches!=1 || group_x!=2 || group_y!=1 || group_z!=1) return fail("start");
    if(rme_vk_gpu_buffer_destroy(&b)!=RME_VK_BUFFER_COMPUTE_ACTIVE || rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_STILL_PENDING || destroyed) return fail("pending ownership");
    wait_result=VK_TIMEOUT;
    if(rme_vk_compute_wait(&s,1)!=RME_VK_COMPUTE_TIMEOUT || rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_STILL_PENDING || destroyed) return fail("timeout retains objects");
    wait_result=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(rme_vk_compute_wait(&s,1)!=RME_VK_COMPUTE_WAIT_ERROR || s.vk_result!=VK_ERROR_OUT_OF_HOST_MEMORY ||
       g.submission_unusable || rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_STILL_PENDING || destroyed) return fail("wait OOM remains retryable");
    wait_result=VK_SUCCESS;
    if(rme_vk_compute_wait(&s,1)!=RME_VK_COMPUTE_OK || rme_vk_gpu_buffer_destroy(&b)!=RME_VK_BUFFER_COMPUTE_ACTIVE ||
       rme_vk_gpu_close(&g)!=RME_VK_DEVICE_BUFFERS_ACTIVE || rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_OK ||
       created!=destroyed || b.compute_references || g.active_submissions) return fail("successful cleanup");
    if(rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_OK || rme_vk_gpu_buffer_destroy(&b)!=RME_VK_BUFFER_OK || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK) return fail("repeat cleanup");
    reset(&g,&b,&s);
    if(START(g,b,s,1)!=RME_VK_COMPUTE_OK) return fail("loss setup");
    wait_result=VK_ERROR_DEVICE_LOST;
    if(rme_vk_compute_wait(&s,1)!=RME_VK_COMPUTE_WAIT_ERROR || s.vk_result!=VK_ERROR_DEVICE_LOST ||
       rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_OK || created!=destroyed || b.compute_references) return fail("lost completion cleanup");
    reset(&g,&b,&s); submit_result=VK_ERROR_DEVICE_LOST;
    if(START(g,b,s,1)!=RME_VK_COMPUTE_SUBMIT_ERROR || rme_vk_compute_dispose(&s)!=RME_VK_COMPUTE_OUTCOME_UNKNOWN ||
       !b.compute_references || !g.active_submissions || created==destroyed) return fail("submit unknown retains objects");
    puts("FAKE_COMPUTE_LIFECYCLE_OK (fake devices, no GPU)"); return 0;
}
