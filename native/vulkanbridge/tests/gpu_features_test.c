/* Feature requests must reach the real device lifecycle without adding features. */
#include "../gpu_device.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const VkPhysicalDevice physical=(VkPhysicalDevice)(uintptr_t)1;
static const VkDevice device=(VkDevice)(uintptr_t)2;
static const VkQueue queue=(VkQueue)(uintptr_t)3;
static VkPhysicalDeviceFeatures available, expected;
static unsigned queries, creates, queues, destroys;
static int expect_features, missing_query, create_failure, invalid_create, empty_queue;

/* The Vulkan 1.0 ABI has 55 VkBool32 fields, allowing this test to construct
 * independent all-field fixtures without sharing the implementation's list. */
_Static_assert(sizeof(VkPhysicalDeviceFeatures)==55*sizeof(VkBool32), "unexpected Vulkan 1.0 feature ABI");
static void set_field(VkPhysicalDeviceFeatures *f,size_t index,VkBool32 value) {
    memcpy((unsigned char *)f + index*sizeof(value),&value,sizeof(value));
}
static void fill_fields(VkPhysicalDeviceFeatures *f,VkBool32 value) {
    for (size_t i=0;i<55;++i) set_field(f,i,value);
}

static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance i,uint32_t *n,VkPhysicalDevice *p) {
    (void)i; if (!p) { *n=1; return VK_SUCCESS; }
    if (*n<1) return VK_INCOMPLETE;
    *n=1; p[0]=physical; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice d,VkPhysicalDeviceProperties *p) {
    (void)d; memset(p,0,sizeof(*p)); p->deviceType=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    p->apiVersion=VK_API_VERSION_1_2;
}
static VKAPI_ATTR void VKAPI_CALL families(VkPhysicalDevice d,uint32_t *n,VkQueueFamilyProperties *p) {
    (void)d; if (!p) { *n=1; return; }
    memset(p,0,sizeof(*p)); p->queueFlags=VK_QUEUE_COMPUTE_BIT; p->queueCount=1; *n=1;
}
static VKAPI_ATTR void VKAPI_CALL features(VkPhysicalDevice d,VkPhysicalDeviceFeatures *p) {
    ++queries; if(d==physical) *p=available; else memset(p,0,sizeof(*p));
}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkPhysicalDevice d,const VkDeviceCreateInfo *ci,
    const VkAllocationCallbacks *a,VkDevice *out) {
    (void)a; ++creates;
    if (d!=physical || !ci || ci->pNext || ci->enabledExtensionCount ||
        ci->queueCreateInfoCount!=1 || ci->pQueueCreateInfos[0].queueFamilyIndex!=0 ||
        (!!ci->pEnabledFeatures)!=expect_features) { invalid_create=1; return VK_ERROR_INITIALIZATION_FAILED; }
    if (expect_features) {
        const VkPhysicalDeviceFeatures *f=ci->pEnabledFeatures;
        if (memcmp(f,&expected,sizeof(expected))!=0) {
            invalid_create=1; return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    if (create_failure) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=device; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL get_queue(VkDevice d,uint32_t family,uint32_t index,VkQueue *out) {
    ++queues; *out=(!empty_queue && d==device && family==0 && index==0) ? queue : VK_NULL_HANDLE;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice d,const VkAllocationCallbacks *a) {
    (void)a; if(d==device) ++destroys;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i,const char *name) {
    (void)i;
    if (!strcmp(name,"vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)enumerate;
    if (!strcmp(name,"vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)properties;
    if (!strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")) return (PFN_vkVoidFunction)families;
    if (!strcmp(name,"vkGetPhysicalDeviceFeatures") && !missing_query) return (PFN_vkVoidFunction)features;
    if (!strcmp(name,"vkCreateDevice")) return (PFN_vkVoidFunction)create;
    if (!strcmp(name,"vkGetDeviceQueue")) return (PFN_vkVoidFunction)get_queue;
    if (!strcmp(name,"vkDestroyDevice")) return (PFN_vkVoidFunction)destroy;
    return NULL;
}
static int fail(const char *s) { fprintf(stderr,"FAIL %s\n",s); return 1; }
static void reset(void) {
    memset(&available,0,sizeof(available)); memset(&expected,0,sizeof(expected));
    queries=creates=queues=destroys=0; missing_query=create_failure=invalid_create=expect_features=empty_queue=0;
}
static enum rme_vk_device_status open_with(VkInstance instance,const VkPhysicalDeviceFeatures *request,
    struct rme_vk_gpu_device *state) {
    return rme_vk_gpu_open_with_features(instance,get_proc,request,state);
}
int main(void) {
    VkInstance instance=(VkInstance)(uintptr_t)1;
    struct rme_vk_gpu_device state={0};
    VkPhysicalDeviceFeatures request={0};
    reset();
    available.robustBufferAccess=VK_TRUE; available.shaderInt64=9; available.inheritedQueries=VK_TRUE;
    available.geometryShader=VK_TRUE; /* supported but deliberately not requested */
    request.robustBufferAccess=VK_TRUE; request.shaderInt64=0xffffffffu; request.inheritedQueries=7;
    expected.robustBufferAccess=expected.shaderInt64=expected.inheritedQueries=VK_TRUE; expect_features=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_OK || invalid_create || queries!=1 || creates!=1 || queues!=1)
        return fail("supported feature request is queried and passed exactly to vkCreateDevice");
    const VkPhysicalDeviceFeatures *enabled=rme_vk_gpu_enabled_features(&state);
    if (!enabled || enabled->robustBufferAccess!=VK_TRUE || enabled->shaderInt64!=VK_TRUE ||
        enabled->inheritedQueries!=VK_TRUE || enabled->geometryShader!=VK_FALSE)
        return fail("enabled query does not match canonical enabled request");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK || rme_vk_gpu_enabled_features(&state) || destroys!=1)
        return fail("close clears enabled features");

    reset(); memset(&request,0,sizeof(request)); request.robustBufferAccess=VK_TRUE;
    request.shaderFloat64=VK_TRUE; available.robustBufferAccess=VK_TRUE;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_FEATURE_UNSUPPORTED || queries!=1 || creates || queues ||
        state.device || rme_vk_gpu_enabled_features(&state)) return fail("unsupported member rejected before creation");

    reset(); missing_query=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_FUNCTION_UNAVAILABLE || creates || queues ||
        state.device || rme_vk_gpu_enabled_features(&state)) return fail("missing feature query rejected before creation");

    reset(); memset(&request,0,sizeof(request)); request.sparseBinding=VK_TRUE;
    available.sparseBinding=VK_TRUE; expected.sparseBinding=VK_TRUE; expect_features=1; create_failure=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_CREATE_FAILED || state.vk_result!=VK_ERROR_OUT_OF_HOST_MEMORY ||
        queries!=1 || creates!=1 || queues || invalid_create || state.device || rme_vk_gpu_enabled_features(&state))
        return fail("device-create error preserved without exposing features");

    reset(); missing_query=1;
    if (rme_vk_gpu_open(instance,get_proc,&state)!=RME_VK_DEVICE_OK || queries || creates!=1 || invalid_create ||
        !rme_vk_gpu_enabled_features(&state) || rme_vk_gpu_enabled_features(&state)->sparseBinding)
        return fail("legacy open keeps null features and no query");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK) return fail("legacy close");

    reset();
    if (open_with(instance,NULL,&state)!=RME_VK_DEVICE_OK || queries || creates!=1 || invalid_create)
        return fail("null feature request remains legacy compatible");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK) return fail("null request close");

    reset(); memset(&request,0,sizeof(request)); expect_features=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_OK || queries!=1 || creates!=1 || invalid_create)
        return fail("explicit empty request queries and enables no features");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK) return fail("explicit empty request close");

    reset(); fill_fields(&available,VK_TRUE); fill_fields(&request,0xffffffffu);
    fill_fields(&expected,VK_TRUE); expect_features=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_OK || queries!=1 || creates!=1 || invalid_create ||
        !rme_vk_gpu_enabled_features(&state) ||
        memcmp(rme_vk_gpu_enabled_features(&state),&expected,sizeof(expected))!=0)
        return fail("all 55 core fields are normalized, enabled, and returned exactly");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK) return fail("all features close");
    for (size_t i=0;i<55;++i) {
        reset(); fill_fields(&available,VK_TRUE); memset(&request,0,sizeof(request));
        set_field(&available,i,VK_FALSE); set_field(&request,i,VK_TRUE);
        if (open_with(instance,&request,&state)!=RME_VK_DEVICE_FEATURE_UNSUPPORTED || queries!=1 || creates ||
            queues || state.device || rme_vk_gpu_enabled_features(&state))
            return fail("each unsupported core field is rejected before creation");
    }

    reset(); expect_features=1; available.shaderInt16=VK_TRUE; expected.shaderInt16=VK_TRUE;
    state.enabled_features.shaderInt16=3;
    if (open_with(instance,&state.enabled_features,&state)!=RME_VK_DEVICE_OK || invalid_create ||
        !rme_vk_gpu_enabled_features(&state) || rme_vk_gpu_enabled_features(&state)->shaderInt16!=VK_TRUE)
        return fail("request aliasing state survives state reset");
    if (rme_vk_gpu_close(&state)!=RME_VK_DEVICE_OK) return fail("aliased request close");

    reset(); memset(&request,0,sizeof(request)); request.shaderInt16=VK_TRUE;
    available.shaderInt16=VK_TRUE; expected.shaderInt16=VK_TRUE; expect_features=empty_queue=1;
    if (open_with(instance,&request,&state)!=RME_VK_DEVICE_QUEUE_UNAVAILABLE || invalid_create ||
        creates!=1 || queues!=1 || destroys!=1 || state.device || rme_vk_gpu_enabled_features(&state))
        return fail("no queue destroys device without exposing enabled features");
    puts("FAKE_VULKAN_DEVICE_FEATURES_OK (fake device; no Guest)"); return 0;
}
