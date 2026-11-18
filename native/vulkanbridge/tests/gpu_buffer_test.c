/* Fake Vulkan dispatch checks control flow; not a real GPU or driver. */
#include "../gpu_buffer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum mode { GOOD, DEDICATED, FAIL_CREATE, NULL_BUFFER, BAD_REQUIREMENTS, TOO_SMALL, BAD_ALIGNMENT, TOO_LARGE,
            BAD_HEAP, SMALL_HEAP, NO_TYPE, FAIL_ALLOC, FAIL_ALLOC_DIRTY, NULL_MEMORY, FAIL_BIND, TOO_MANY_TYPES,
            LAZY_FIRST, AMD_COHERENT_FIRST, AMD_UNCACHED_FIRST, LAZY_ONLY, AMD_COHERENT_ONLY, AMD_UNCACHED_ONLY };
static enum mode mode;
static const char *missing;
static char events[16]; static unsigned n_events;
static const VkInstance instance=(VkInstance)(uintptr_t)1;
static const VkPhysicalDevice physical=(VkPhysicalDevice)(uintptr_t)2;
static const VkDevice device=(VkDevice)(uintptr_t)3;
static const VkQueue queue=(VkQueue)(uintptr_t)4;
static const VkBuffer buffer=(VkBuffer)(uintptr_t)5;
static const VkDeviceMemory memory=(VkDeviceMemory)(uintptr_t)6;
static void event(char c) { if (n_events<sizeof(events)-1) events[n_events++]=c; events[n_events]=0; }
static VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice d,const VkBufferCreateInfo *ci,const VkAllocationCallbacks *a,VkBuffer *out) {
    (void)a; event('C');
    if (d!=device || ci->size!=256 || ci->usage!=(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || ci->sharingMode!=VK_SHARING_MODE_EXCLUSIVE) return VK_ERROR_INITIALIZATION_FAILED;
    if (mode==FAIL_CREATE) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=mode==NULL_BUFFER ? VK_NULL_HANDLE : buffer; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks *a) { (void)a; if (d==device && b==buffer) event('D'); }
static VKAPI_ATTR void VKAPI_CALL requirements(VkDevice d,const VkBufferMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {
    VkMemoryDedicatedRequirements *dedicated=out->pNext;
    if(d!=device || !info || info->buffer!=buffer || !dedicated ||
       dedicated->sType!=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) return;
    VkMemoryRequirements *r=&out->memoryRequirements;
    r->size=mode==BAD_REQUIREMENTS ? 0 : mode==TOO_SMALL ? 128 : mode==TOO_LARGE ? 2048 : 512;
    r->alignment=mode==BAD_ALIGNMENT ? 3 : 64;
    r->memoryTypeBits=(mode==NO_TYPE || mode==LAZY_ONLY || mode==AMD_COHERENT_ONLY || mode==AMD_UNCACHED_ONLY) ? 1 : 3;
    dedicated->requiresDedicatedAllocation=mode==DEDICATED ? VK_TRUE : VK_FALSE;
}
static VKAPI_ATTR void VKAPI_CALL memory_properties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *m) {
    memset(m,0,sizeof(*m)); if(p!=physical) return;
    m->memoryTypeCount=mode==TOO_MANY_TYPES ? VK_MAX_MEMORY_TYPES+1 : 2;
    m->memoryHeapCount=1; m->memoryHeaps[0].size=mode==SMALL_HEAP ? 256 : 4096;
    m->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    m->memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if(mode==LAZY_FIRST || mode==LAZY_ONLY)
        m->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    if(mode==AMD_COHERENT_FIRST || mode==AMD_COHERENT_ONLY)
        m->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
    if(mode==AMD_UNCACHED_FIRST || mode==AMD_UNCACHED_ONLY)
        m->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD|VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;
    if (mode==BAD_HEAP) m->memoryTypes[1].heapIndex=1;
}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkMemoryAllocateInfo *a,const VkAllocationCallbacks *cb,VkDeviceMemory *out) {
    (void)cb; event('A'); if(d!=device || a->allocationSize!=512 || a->memoryTypeIndex!=1) return VK_ERROR_INITIALIZATION_FAILED;
    const VkMemoryDedicatedAllocateInfo *dedicated=a->pNext;
    if (mode==DEDICATED) {
        if (!dedicated || dedicated->sType!=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO || dedicated->buffer!=buffer || dedicated->image!=VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    } else if (dedicated) return VK_ERROR_INITIALIZATION_FAILED;
    if(mode==FAIL_ALLOC_DIRTY) { *out=(VkDeviceMemory)(uintptr_t)9; return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    if(mode==FAIL_ALLOC) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out=mode==NULL_MEMORY ? VK_NULL_HANDLE : memory; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a) { (void)a; if(d==device) event(m==memory?'F':'X'); }
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d,const VkAllocationCallbacks *a) { (void)a; if(d==device) event('G'); }
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize off) {
    event('B'); if(d!=device || b!=buffer || m!=memory || off) return VK_ERROR_INITIALIZATION_FAILED;
    return mode==FAIL_BIND ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_device_proc(VkDevice d,const char *name) {
    if (d!=device || (missing && !strcmp(name,missing))) return NULL;
    if(!strcmp(name,"vkCreateBuffer")) return (PFN_vkVoidFunction)create_buffer;
    if(!strcmp(name,"vkDestroyBuffer")) return (PFN_vkVoidFunction)destroy_buffer;
    if(!strcmp(name,"vkGetBufferMemoryRequirements2")) return (PFN_vkVoidFunction)requirements;
    if(!strcmp(name,"vkAllocateMemory")) return (PFN_vkVoidFunction)allocate;
    if(!strcmp(name,"vkFreeMemory")) return (PFN_vkVoidFunction)free_memory;
    if(!strcmp(name,"vkBindBufferMemory")) return (PFN_vkVoidFunction)bind;
    return NULL;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i,const char *name) {
    if (i!=instance || (missing && !strcmp(name,missing))) return NULL;
    if (!strcmp(name,"vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)get_device_proc;
    if (!strcmp(name,"vkGetPhysicalDeviceMemoryProperties")) return (PFN_vkVoidFunction)memory_properties;
    return NULL;
}
static void reset(enum mode m) { mode=m; missing=NULL; n_events=0; events[0]=0; }
static void setup_gpu(struct rme_vk_gpu_device *g) {
    memset(g,0,sizeof(*g)); g->device=device; g->queue=queue; g->destroy=destroy_device;
    g->selected.device=physical; g->selected.status=RME_VK_SELECT_OK;
    g->selected.properties.deviceType=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    g->selected.properties.apiVersion=VK_API_VERSION_1_2;
    g->selected.properties.limits.maxMemoryAllocationCount=2;
}
static int expect(enum mode m,enum rme_vk_buffer_status status,const char *sequence,VkResult vk) {
    struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(m);
    setup_gpu(&g);
    enum rme_vk_buffer_status actual=rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s);
    if(actual!=status || s.status!=status || s.vk_result!=vk || strcmp(events,sequence)) {
        fprintf(stderr,"FAIL mode=%d status=%d vk=%d events=%s\n",m,actual,s.vk_result,events); return 1;
    }
    if(status==RME_VK_BUFFER_OK) {
        if(s.buffer!=buffer || s.memory!=memory || s.owner!=&g || g.active_buffer_allocations!=1 || s.allocation_size!=512 || s.memory_type_index!=1 || s.memory_heap_index!=0 || s.dedicated!=(m==DEDICATED) ||
           !(s.memory_properties&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return 1;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s)!=RME_VK_BUFFER_ALREADY_OPEN) return 1;
        if(rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_OK || strcmp(events,"CABDF") || s.buffer || s.memory || g.active_buffer_allocations) return 1;
        if(rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_OK || strcmp(events,"CABDF")) return 1;
    } else if(s.buffer || s.memory || g.active_buffer_allocations) return 1;
    return 0;
}
int main(void) {
    {
        struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(GOOD); setup_gpu(&g);
        g.submission_unusable=VK_TRUE; g.submission_error=VK_ERROR_DEVICE_LOST;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s)!=RME_VK_BUFFER_DEVICE_UNAVAILABLE || events[0] || s.vk_result!=VK_ERROR_DEVICE_LOST) { fprintf(stderr,"FAIL lost device blocks buffer\n"); return 1; }
        g.submission_unusable=VK_FALSE;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s)!=RME_VK_BUFFER_OK) return 1;
        g.active_submissions=1;
        if(rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_SUBMISSIONS_ACTIVE || strcmp(events,"CAB")) { fprintf(stderr,"FAIL pending buffer lifetime\n"); return 1; }
        g.active_submissions=0;
        if(rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_OK || strcmp(events,"CABDF")) return 1;
    }
    int failed=0;
    failed |= expect(GOOD,RME_VK_BUFFER_OK,"CAB",VK_SUCCESS);
    failed |= expect(DEDICATED,RME_VK_BUFFER_OK,"CAB",VK_SUCCESS);
    failed |= expect(FAIL_CREATE,RME_VK_BUFFER_CREATE_FAILED,"C",VK_ERROR_OUT_OF_HOST_MEMORY);
    failed |= expect(NULL_BUFFER,RME_VK_BUFFER_CREATE_FAILED,"C",VK_SUCCESS);
    failed |= expect(BAD_REQUIREMENTS,RME_VK_BUFFER_INVALID_REQUIREMENTS,"CD",VK_SUCCESS);
    failed |= expect(TOO_SMALL,RME_VK_BUFFER_INVALID_REQUIREMENTS,"CD",VK_SUCCESS);
    failed |= expect(BAD_ALIGNMENT,RME_VK_BUFFER_INVALID_REQUIREMENTS,"CD",VK_SUCCESS);
    failed |= expect(TOO_LARGE,RME_VK_BUFFER_LIMIT_EXCEEDED,"CD",VK_SUCCESS);
    failed |= expect(BAD_HEAP,RME_VK_BUFFER_INVALID_REQUIREMENTS,"CD",VK_SUCCESS);
    failed |= expect(SMALL_HEAP,RME_VK_BUFFER_NO_MEMORY_TYPE,"CD",VK_SUCCESS);
    failed |= expect(NO_TYPE,RME_VK_BUFFER_NO_MEMORY_TYPE,"CD",VK_SUCCESS);
    failed |= expect(TOO_MANY_TYPES,RME_VK_BUFFER_INVALID_REQUIREMENTS,"CD",VK_SUCCESS);
    failed |= expect(FAIL_ALLOC,RME_VK_BUFFER_ALLOCATE_FAILED,"CAD",VK_ERROR_OUT_OF_DEVICE_MEMORY);
    failed |= expect(FAIL_ALLOC_DIRTY,RME_VK_BUFFER_ALLOCATE_FAILED,"CAD",VK_ERROR_OUT_OF_DEVICE_MEMORY);
    failed |= expect(NULL_MEMORY,RME_VK_BUFFER_ALLOCATE_FAILED,"CAD",VK_SUCCESS);
    failed |= expect(FAIL_BIND,RME_VK_BUFFER_BIND_FAILED,"CABDF",VK_ERROR_OUT_OF_DEVICE_MEMORY);
    const char *names[]={"vkGetDeviceProcAddr","vkCreateBuffer","vkDestroyBuffer","vkGetBufferMemoryRequirements2", "vkGetPhysicalDeviceMemoryProperties","vkAllocateMemory","vkFreeMemory","vkBindBufferMemory"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(GOOD); missing=names[i];
        setup_gpu(&g);
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_FUNCTION_UNAVAILABLE || n_events) { fprintf(stderr,"FAIL missing %s\n",names[i]); failed=1; }
    }
    {
        struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(GOOD);
        setup_gpu(&g);
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,0,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_INVALID_ARGUMENT ||
           rme_vk_gpu_buffer_create(instance,get_proc,&g,256,128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_LIMIT_EXCEEDED ||
           rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,0,0,&s)!=RME_VK_BUFFER_INVALID_ARGUMENT ||
           rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_INVALID_ARGUMENT ||
           rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_PROTECTED_BIT,&s)!=RME_VK_BUFFER_INVALID_ARGUMENT || n_events) { fprintf(stderr,"FAIL invalid arguments\n"); failed=1; }
        g.selected.properties.deviceType=VK_PHYSICAL_DEVICE_TYPE_CPU;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_INVALID_ARGUMENT || n_events) { fprintf(stderr,"FAIL CPU state accepted\n"); failed=1; }
        if(rme_vk_gpu_buffer_destroy(NULL)!=RME_VK_BUFFER_INVALID_ARGUMENT) failed=1;
    }
    {
        const enum mode cases[]={LAZY_FIRST,AMD_COHERENT_FIRST,AMD_UNCACHED_FIRST,LAZY_ONLY,AMD_COHERENT_ONLY,AMD_UNCACHED_ONLY};
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
            struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(cases[i]);
            setup_gpu(&g);
            enum rme_vk_buffer_status got=rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,0,&s);
            if(i<3) {
                if(got!=RME_VK_BUFFER_OK || s.memory_type_index!=1 || rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_OK || strcmp(events,"CABDF")) { fprintf(stderr,"FAIL unsupported memory fallback case %u\n",i); failed=1; }
            } else if(got!=RME_VK_BUFFER_NO_MEMORY_TYPE || strcmp(events,"CD")) { fprintf(stderr,"FAIL unsupported memory only case %u\n",i); failed=1; }
        }
    }
    {
        struct rme_vk_gpu_device g={0}; struct rme_vk_gpu_buffer s={0}; reset(GOOD); setup_gpu(&g);
        g.selected.properties.limits.maxMemoryAllocationCount=1; g.active_buffer_allocations=1;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,0,&s)!=RME_VK_BUFFER_LIMIT_EXCEEDED || n_events) { fprintf(stderr,"FAIL allocation count limit\n"); failed=1; }
        g.active_buffer_allocations=0;
        if(rme_vk_gpu_buffer_create(instance,get_proc,&g,256,1024,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&s)!=RME_VK_BUFFER_OK || g.active_buffer_allocations!=1 ||
           rme_vk_gpu_close(&g)!=RME_VK_DEVICE_BUFFERS_ACTIVE || strcmp(events,"CAB") ||
           rme_vk_gpu_buffer_destroy(&s)!=RME_VK_BUFFER_OK || g.active_buffer_allocations ||
           rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK || strcmp(events,"CABDFG")) { fprintf(stderr,"FAIL active buffer prevents device close\n"); failed=1; }
    }
    if(failed) return 1;
    puts("FAKE_VULKAN_BUFFER_LIFECYCLE_OK (fake devices, not a GPU)"); return 0;
}
