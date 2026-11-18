/* Fake Vulkan dispatch for service-process tests. It is not a GPU. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int mode(const char *s) { const char *v=getenv("RME_VK_FAKE_CASE"); return v && !strcmp(v,s); }
static VkInstance fi(void) { return (VkInstance)(uintptr_t)0x11; }
static VkPhysicalDevice fp(void) { return (VkPhysicalDevice)(uintptr_t)0x22; }
static VkDevice fd(void) { return (VkDevice)(uintptr_t)0x33; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *ci,
    const VkAllocationCallbacks *a, VkInstance *out) {
    (void)a;
    fprintf(stderr,"fake:instance-create\n");
    if (mode("hang_instance")) for (;;) pause();
    if (!ci || !ci->pApplicationInfo || ci->pApplicationInfo->apiVersion != VK_API_VERSION_1_2 || !out)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (mode("instance_fail")) return VK_ERROR_INITIALIZATION_FAILED;
    *out=fi(); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance i, const VkAllocationCallbacks *a) {
    (void)i; (void)a; fprintf(stderr,"fake:instance-destroy\n");
}
static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance i, uint32_t *count, VkPhysicalDevice *out) {
    if (i!=fi() || !count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!out) { *count=1; return VK_SUCCESS; }
    if (!*count) return VK_INCOMPLETE;
    out[0]=fp(); *count=1; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice p, VkPhysicalDeviceProperties *out) {
    (void)p; memset(out,0,sizeof(*out)); out->apiVersion=VK_API_VERSION_1_2;
    out->deviceType=mode("cpu") ? VK_PHYSICAL_DEVICE_TYPE_CPU : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    memcpy(out->deviceName, "fake\nGPU", sizeof("fake\nGPU"));
}
static VKAPI_ATTR void VKAPI_CALL queues(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *out) {
    (void)p;
    if (!out) { *count=1; return; }
    if (!*count) return;
    memset(out,0,sizeof(*out)); out->queueFlags=VK_QUEUE_COMPUTE_BIT; out->queueCount=1; *count=1;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice p,const VkDeviceCreateInfo *ci,
    const VkAllocationCallbacks *a,VkDevice *out) {
    (void)p; (void)ci; (void)a; fprintf(stderr,"fake:device-create\n");
    if (mode("device_fail")) return VK_ERROR_INITIALIZATION_FAILED;
    *out=fd(); return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL get_queue(VkDevice d,uint32_t f,uint32_t q,VkQueue *out) {
    (void)d; (void)f; (void)q; *out=(VkQueue)(uintptr_t)0x44;
}
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d,const VkAllocationCallbacks *a) {
    (void)d; (void)a; fprintf(stderr,"fake:device-destroy\n");
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char *name) {
    (void)i;
#define PROVIDE(n,f) if (!strcmp(name,n)) return (PFN_vkVoidFunction)(f)
    PROVIDE("vkEnumeratePhysicalDevices",enumerate);
    PROVIDE("vkGetPhysicalDeviceProperties",properties);
    PROVIDE("vkGetPhysicalDeviceQueueFamilyProperties",queues);
    PROVIDE("vkCreateDevice",create_device);
    PROVIDE("vkGetDeviceQueue",get_queue);
    PROVIDE("vkDestroyDevice",destroy_device);
#undef PROVIDE
    return NULL;
}
