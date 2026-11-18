/* Fake Vulkan loader exercises lifetime and failures; no real GPU involved. */
#include "../gpu_device.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum mode { GOOD, CPU, NO_CREATE, NO_GET_QUEUE, NO_DESTROY, CREATE_FAIL, NULL_DEVICE, NULL_QUEUE };
static enum mode mode;
static unsigned creates, destroys, gets;
static const VkPhysicalDevice fake_physical=(VkPhysicalDevice)(uintptr_t)1;
static const VkDevice fake_device=(VkDevice)(uintptr_t)2;
static const VkQueue fake_queue=(VkQueue)(uintptr_t)3;
static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance i, uint32_t *n, VkPhysicalDevice *p) {
    (void)i; if (!p) { *n=1; return VK_SUCCESS; } if (*n<1) return VK_INCOMPLETE;
    p[0]=fake_physical; *n=1; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice d, VkPhysicalDeviceProperties *p) {
    (void)d; memset(p,0,sizeof(*p)); p->apiVersion=VK_API_VERSION_1_2;
    p->deviceType=mode==CPU ? VK_PHYSICAL_DEVICE_TYPE_CPU : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
}
static VKAPI_ATTR void VKAPI_CALL families(VkPhysicalDevice d,uint32_t *n,VkQueueFamilyProperties *p) {
    (void)d; if (!p) { *n=2; return; } if (*n<2) { *n=0; return; }
    memset(p,0,2*sizeof(*p)); p[1].queueFlags=VK_QUEUE_COMPUTE_BIT; p[1].queueCount=1; *n=2;
}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkPhysicalDevice d,const VkDeviceCreateInfo *ci,
    const VkAllocationCallbacks *a,VkDevice *out) {
    (void)a; ++creates;
    if (d!=fake_physical || !ci || ci->queueCreateInfoCount!=1 ||
        ci->pNext!=NULL || ci->enabledExtensionCount!=0 || ci->ppEnabledExtensionNames!=NULL ||
        ci->pEnabledFeatures!=NULL ||
        ci->pQueueCreateInfos[0].queueFamilyIndex!=1 || ci->pQueueCreateInfos[0].queueCount!=1 ||
        ci->pQueueCreateInfos[0].pQueuePriorities[0]!=1.0f) return VK_ERROR_INITIALIZATION_FAILED;
    if (mode==CREATE_FAIL) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (mode==NULL_DEVICE) { *out=VK_NULL_HANDLE; return VK_SUCCESS; }
    *out=fake_device; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL get_queue(VkDevice d,uint32_t family,uint32_t index,VkQueue *out) {
    ++gets; *out=(mode!=NULL_QUEUE && d==fake_device && family==1 && index==0) ? fake_queue : VK_NULL_HANDLE;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice d,const VkAllocationCallbacks *a) {
    (void)a; if (d==fake_device) ++destroys;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i,const char *name) {
    (void)i;
    if (!strcmp(name,"vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)enumerate;
    if (!strcmp(name,"vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)properties;
    if (!strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")) return (PFN_vkVoidFunction)families;
    if (!strcmp(name,"vkCreateDevice") && mode!=NO_CREATE) return (PFN_vkVoidFunction)create;
    if (!strcmp(name,"vkGetDeviceQueue") && mode!=NO_GET_QUEUE) return (PFN_vkVoidFunction)get_queue;
    if (!strcmp(name,"vkDestroyDevice") && mode!=NO_DESTROY) return (PFN_vkVoidFunction)destroy;
    return NULL;
}
static int fail(const char *s) { fprintf(stderr,"FAIL %s\n",s); return 1; }
int main(void) {
    VkInstance instance=(VkInstance)(uintptr_t)1;
    struct rme_vk_gpu_device s={0};
    mode=GOOD;
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_OK || s.device!=fake_device || s.queue!=fake_queue || creates!=1 || gets!=1) return fail("open");
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_ALREADY_OPEN || creates!=1 || s.device!=fake_device) return fail("double open");
    if (rme_vk_gpu_close(&s)!=RME_VK_DEVICE_OK || destroys!=1 || s.device || s.queue) return fail("close");
    if (rme_vk_gpu_close(&s)!=RME_VK_DEVICE_OK || destroys!=1) return fail("double close");
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_OK || creates!=2 || gets!=2 || s.device!=fake_device) return fail("reopen");
    if (rme_vk_gpu_close(&s)!=RME_VK_DEVICE_OK || destroys!=2 || s.device || s.queue) return fail("reclose");
    enum mode missing[]={NO_CREATE,NO_GET_QUEUE,NO_DESTROY};
    for (unsigned i=0;i<3;i++) {
        memset(&s,0,sizeof(s)); mode=missing[i]; creates=gets=destroys=0;
        if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_FUNCTION_UNAVAILABLE || creates || gets || destroys || s.device || s.queue) return fail("missing entrypoint");
    }
    memset(&s,0,sizeof(s)); mode=CPU; creates=gets=destroys=0;
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_SELECT_FAILED || s.select_status!=RME_VK_SELECT_NO_SUITABLE_DEVICE || creates || gets || destroys) return fail("CPU rejection");
    memset(&s,0,sizeof(s)); mode=CREATE_FAIL; creates=gets=destroys=0;
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_CREATE_FAILED || s.vk_result!=VK_ERROR_OUT_OF_HOST_MEMORY || creates!=1 || gets || destroys || s.device) return fail("create error");
    memset(&s,0,sizeof(s)); mode=NULL_DEVICE; creates=gets=destroys=0;
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_CREATE_FAILED || s.vk_result!=VK_SUCCESS || creates!=1 || gets || destroys || s.device) return fail("null device on success");
    memset(&s,0,sizeof(s)); mode=NULL_QUEUE; creates=gets=destroys=0;
    if (rme_vk_gpu_open(instance,get_proc,&s)!=RME_VK_DEVICE_QUEUE_UNAVAILABLE || creates!=1 || gets!=1 || destroys!=1 || s.device || s.queue) return fail("null queue cleanup");
    if (rme_vk_gpu_close(NULL)!=RME_VK_DEVICE_INVALID_ARGUMENT || rme_vk_gpu_open(instance,get_proc,NULL)!=RME_VK_DEVICE_INVALID_ARGUMENT) return fail("null state");
    memset(&s,0,sizeof(s)); mode=GOOD; creates=gets=destroys=0;
    if (rme_vk_gpu_open(VK_NULL_HANDLE,get_proc,&s)!=RME_VK_DEVICE_INVALID_ARGUMENT ||
        rme_vk_gpu_open(instance,NULL,&s)!=RME_VK_DEVICE_INVALID_ARGUMENT || creates || gets || destroys) return fail("invalid open argument");
    memset(&s,0,sizeof(s)); s.device=fake_device;
    if (rme_vk_gpu_close(&s)!=RME_VK_DEVICE_INVALID_STATE || s.device!=fake_device || destroys) return fail("invalid state must preserve handle");
    puts("FAKE_VULKAN_DEVICE_LIFECYCLE_OK (fake devices, not a GPU)"); return 0;
}
