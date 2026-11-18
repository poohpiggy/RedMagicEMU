/* Fake Vulkan functions test selection behavior; they are not a real GPU. */
#include "../gpu_select.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

enum scenario { MIXED, CPU_ONLY, VIRTUAL_ONLY, UNKNOWN_TYPE, DISCRETE, OLD_GPU, ALTERNATE_VARIANT,
    NO_COMPUTE, EMPTY, FAIL_ENUM, FAIL_FILL, MISSING, OVER_LIMIT, QUEUE_LIMIT, QUEUE_GROWS, QUEUE_SHRINKS,
    INCOMPLETE_ONCE, INCOMPLETE_ALWAYS, COMPUTE_BEFORE_SHARED, SHARED_FIRST,
    EMPTY_SHARED, EMPTY_COMPUTE_QUEUES, MULTI_GPU_KEEP_FIRST };
static enum scenario mode;
static unsigned fill_calls, queue_count_queries;
static VkPhysicalDevice handle(unsigned n) { return (VkPhysicalDevice)(uintptr_t)(n+1); }
static unsigned index_of(VkPhysicalDevice d) { return (unsigned)((uintptr_t)d-1); }
static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance instance, uint32_t *count, VkPhysicalDevice *devices) {
    (void)instance;
    if (mode == FAIL_ENUM) return VK_ERROR_INITIALIZATION_FAILED;
    if (mode == OVER_LIMIT) { *count = 257; return VK_SUCCESS; }
    unsigned n = mode == EMPTY ? 0 : mode == MIXED ? 3 : mode == MULTI_GPU_KEEP_FIRST ? 2 : 1;
    if (!devices) { *count = n; return VK_SUCCESS; }
    if (mode == FAIL_FILL) return VK_ERROR_DEVICE_LOST;
    if (mode == INCOMPLETE_ALWAYS || (mode == INCOMPLETE_ONCE && !fill_calls++)) { *count = 0; return VK_INCOMPLETE; }
    if (*count < n) return VK_INCOMPLETE;
    for (unsigned i=0;i<n;i++) devices[i]=handle(i);
    *count=n; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice d, VkPhysicalDeviceProperties *p) {
    memset(p,0,sizeof(*p));
    p->apiVersion = mode == OLD_GPU ? VK_API_VERSION_1_1 :
        mode == ALTERNATE_VARIANT ? VK_MAKE_API_VERSION(1,1,2,0) : VK_API_VERSION_1_2;
    p->deviceType = mode == VIRTUAL_ONLY ? VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU :
        mode == UNKNOWN_TYPE ? VK_PHYSICAL_DEVICE_TYPE_OTHER :
        mode == DISCRETE ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU :
        mode == CPU_ONLY || (mode == MIXED && index_of(d)==0) ? VK_PHYSICAL_DEVICE_TYPE_CPU :
        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}
static VKAPI_ATTR void VKAPI_CALL queues(VkPhysicalDevice d, uint32_t *count, VkQueueFamilyProperties *q) {
    if (!q) {
        unsigned query = queue_count_queries++;
        *count=mode == QUEUE_LIMIT ? 257 :
               query && mode == QUEUE_GROWS ? 3 :
               query && mode == QUEUE_SHRINKS ? 1 : 2;
        return;
    }
    if (*count<2) { *count=0; return; }
    memset(q,0,2*sizeof(*q)); q[0].queueFlags=VK_QUEUE_GRAPHICS_BIT; q[0].queueCount=1;
    q[1].queueFlags=VK_QUEUE_COMPUTE_BIT;
    q[1].queueCount=(mode == NO_COMPUTE || (mode == MIXED && index_of(d)==1)) ? 0 : 1;
    if (mode == COMPUTE_BEFORE_SHARED) {
        q[0].queueFlags=VK_QUEUE_COMPUTE_BIT;
        q[1].queueFlags=VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    } else if (mode == SHARED_FIRST || mode == EMPTY_SHARED) {
        q[0].queueFlags=VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if (mode == EMPTY_SHARED) q[0].queueCount=0;
    } else if (mode == EMPTY_COMPUTE_QUEUES) {
        q[0].queueFlags=VK_QUEUE_COMPUTE_BIT;
        q[1].queueFlags=VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        q[0].queueCount=0; q[1].queueCount=0;
    } else if (mode == MULTI_GPU_KEEP_FIRST && index_of(d)==1) {
        q[1].queueFlags=VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    }
    *count=2;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance instance, const char *name) {
    (void)instance;
    if (!strcmp(name,"vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)enumerate;
    if (!strcmp(name,"vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)properties;
    if (!strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties") && mode != MISSING) return (PFN_vkVoidFunction)queues;
    return NULL;
}
static int run(enum scenario s, enum rme_vk_select_status want) {
    struct rme_vk_gpu_selection out; memset(&out,0x5a,sizeof(out));
    mode=s; fill_calls=0; queue_count_queries=0;
    enum rme_vk_select_status got=rme_vk_select_gpu((VkInstance)(uintptr_t)1,get_proc,&out);
    if (got!=want || out.status!=want) { fprintf(stderr,"FAIL case %d status=%d expected=%d\n",s,got,want); return 1; }
    uint32_t wanted_family = s==SHARED_FIRST ? 0 : 1;
    if (want==RME_VK_SELECT_OK && (out.device!=handle(s==MIXED?2:0) || out.queue_family!=wanted_family || out.properties.apiVersion!=VK_API_VERSION_1_2)) {
        fprintf(stderr,"FAIL case %d selected queue=%u expected=%u or wrong device\n",s,out.queue_family,wanted_family); return 1;
    }
    if (want!=RME_VK_SELECT_OK && out.device!=VK_NULL_HANDLE) { fprintf(stderr,"FAIL case %d left stale device\n",s); return 1; }
    if (s==FAIL_ENUM && out.vk_result!=VK_ERROR_INITIALIZATION_FAILED) { fprintf(stderr,"FAIL original Vulkan error lost\n"); return 1; }
    if (s==FAIL_FILL && out.vk_result!=VK_ERROR_DEVICE_LOST) { fprintf(stderr,"FAIL fill error lost\n"); return 1; }
    if (s==INCOMPLETE_ALWAYS && out.vk_result!=VK_INCOMPLETE) { fprintf(stderr,"FAIL repeated incomplete lost\n"); return 1; }
    return 0;
}
int main(void) {
    int failed=0;
    failed |= run(MIXED,RME_VK_SELECT_OK);
    failed |= run(CPU_ONLY,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(VIRTUAL_ONLY,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(UNKNOWN_TYPE,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(DISCRETE,RME_VK_SELECT_OK);
    failed |= run(OLD_GPU,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(ALTERNATE_VARIANT,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(NO_COMPUTE,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(EMPTY,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(FAIL_ENUM,RME_VK_SELECT_ENUMERATION_FAILED);
    failed |= run(FAIL_FILL,RME_VK_SELECT_ENUMERATION_FAILED);
    failed |= run(MISSING,RME_VK_SELECT_FUNCTION_UNAVAILABLE);
    failed |= run(OVER_LIMIT,RME_VK_SELECT_LIMIT_EXCEEDED);
    failed |= run(QUEUE_LIMIT,RME_VK_SELECT_LIMIT_EXCEEDED);
    failed |= run(QUEUE_GROWS,RME_VK_SELECT_INVALID_RESPONSE);
    failed |= run(QUEUE_SHRINKS,RME_VK_SELECT_INVALID_RESPONSE);
    failed |= run(INCOMPLETE_ONCE,RME_VK_SELECT_OK);
    failed |= run(INCOMPLETE_ALWAYS,RME_VK_SELECT_ENUMERATION_FAILED);
    failed |= run(COMPUTE_BEFORE_SHARED,RME_VK_SELECT_OK);
    failed |= run(SHARED_FIRST,RME_VK_SELECT_OK);
    failed |= run(EMPTY_SHARED,RME_VK_SELECT_OK);
    failed |= run(EMPTY_COMPUTE_QUEUES,RME_VK_SELECT_NO_SUITABLE_DEVICE);
    failed |= run(MULTI_GPU_KEEP_FIRST,RME_VK_SELECT_OK);
    struct rme_vk_gpu_selection out;
    if (rme_vk_select_gpu(VK_NULL_HANDLE,get_proc,&out)!=RME_VK_SELECT_INVALID_ARGUMENT || out.device!=VK_NULL_HANDLE ||
        rme_vk_select_gpu((VkInstance)(uintptr_t)1,NULL,&out)!=RME_VK_SELECT_INVALID_ARGUMENT ||
        rme_vk_select_gpu((VkInstance)(uintptr_t)1,get_proc,NULL)!=RME_VK_SELECT_INVALID_ARGUMENT) failed=1;
    if (failed) return 1;
    puts("FAKE_VULKAN_SELECTION_TEST_OK (fake devices, not a GPU)"); return 0;
}
