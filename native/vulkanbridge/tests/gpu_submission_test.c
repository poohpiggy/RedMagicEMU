/* Fake dispatch checks lifecycle; it does not execute GPU work. */
#include "../gpu_submission.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum mode { GOOD, CREATE_FAIL, CREATE_LOST, NULL_FENCE, SUBMIT_OOM, SUBMIT_LOST, WAIT_TIMEOUT, WAIT_LOST, WAIT_ERROR, WAIT_DEVICE_OOM, WAIT_OTHER };
static enum mode mode; static const char *missing;
static unsigned creates, submits, waits, destroys, device_destroys;
static const VkInstance instance=(VkInstance)(uintptr_t)1;
static const VkPhysicalDevice physical=(VkPhysicalDevice)(uintptr_t)2;
static const VkDevice device=(VkDevice)(uintptr_t)3;
static const VkQueue queue=(VkQueue)(uintptr_t)4;
static const VkFence fence=(VkFence)(uintptr_t)5;
static const VkCommandBuffer command=(VkCommandBuffer)(uintptr_t)6;
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice d,const VkFenceCreateInfo *ci,const VkAllocationCallbacks *a,VkFence *out) {
    (void)a; ++creates; if(d!=device || !ci || ci->pNext || ci->flags) return VK_ERROR_INITIALIZATION_FAILED;
    if(mode==CREATE_FAIL) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode==CREATE_LOST) return VK_ERROR_DEVICE_LOST;
    *out=mode==NULL_FENCE ? VK_NULL_HANDLE : (VkFence)((uintptr_t)fence+creates-1); return VK_SUCCESS;
}
static int fake_fence(VkFence f) { return (uintptr_t)f >= (uintptr_t)fence && (uintptr_t)f < (uintptr_t)fence+creates; }
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice d,VkFence f,const VkAllocationCallbacks *a) { (void)a; if(d==device&&fake_fence(f)) ++destroys; }
static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f) {
    ++submits; if(q!=queue || n!=1 || !s || !fake_fence(f) || s->waitSemaphoreCount || s->signalSemaphoreCount ||
        s->commandBufferCount>1 || (s->commandBufferCount==1 && (!s->pCommandBuffers || *s->pCommandBuffers!=command)) ||
        (!s->commandBufferCount && s->pCommandBuffers)) return VK_ERROR_INITIALIZATION_FAILED;
    if(mode==SUBMIT_OOM) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode==SUBMIT_LOST) return VK_ERROR_DEVICE_LOST;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL wait_fences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t timeout) {
    (void)timeout; ++waits; if(d!=device || n!=1 || !f || !fake_fence(*f) || all!=VK_TRUE) return VK_ERROR_INITIALIZATION_FAILED;
    if(mode==WAIT_TIMEOUT) return VK_TIMEOUT;
    if(mode==WAIT_LOST) return VK_ERROR_DEVICE_LOST;
    if(mode==WAIT_ERROR) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode==WAIT_DEVICE_OOM) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if(mode==WAIT_OTHER) return VK_ERROR_UNKNOWN;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice d,const VkAllocationCallbacks *a) { (void)a; if(d==device) ++device_destroys; }
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice d,const char *name) {
    if(d!=device || (missing && !strcmp(name,missing))) return NULL;
    if(!strcmp(name,"vkCreateFence")) return (PFN_vkVoidFunction)create;
    if(!strcmp(name,"vkDestroyFence")) return (PFN_vkVoidFunction)destroy;
    if(!strcmp(name,"vkQueueSubmit")) return (PFN_vkVoidFunction)submit;
    if(!strcmp(name,"vkWaitForFences")) return (PFN_vkVoidFunction)wait_fences;
    return NULL;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i,const char *name) {
    if(i==instance && !strcmp(name,"vkGetDeviceProcAddr") && (!missing || strcmp(name,missing))) return (PFN_vkVoidFunction)gdpa;
    return NULL;
}
static void reset(enum mode m,struct rme_vk_gpu_device *g,struct rme_vk_gpu_submission *s) {
    mode=m; missing=NULL; creates=submits=waits=destroys=device_destroys=0;
    memset(g,0,sizeof(*g)); memset(s,0,sizeof(*s));
    g->device=device; g->queue=queue; g->destroy=destroy_device; g->selected.device=physical;
    g->selected.properties.deviceType=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    g->selected.properties.apiVersion=VK_API_VERSION_1_2;
}
static int fail(const char *name) { fprintf(stderr,"FAIL %s\n",name); return 1; }
int main(void) {
    struct rme_vk_gpu_device g; struct rme_vk_gpu_submission s;
    reset(GOOD,&g,&s);
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_OK || submits!=1 || g.active_submissions!=1) return fail("submit recorded command");
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_ALREADY_ACTIVE || submits!=1) return fail("double submit");
    if(rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_STILL_PENDING || destroys || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE || device_destroys) return fail("pending lifetime");
    mode=WAIT_TIMEOUT;
    if(rme_vk_gpu_submission_wait(&s,100)!=RME_VK_SUBMISSION_TIMEOUT || s.vk_result!=VK_TIMEOUT ||
       rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_STILL_PENDING || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE || destroys || device_destroys) return fail("timeout lifetime");
    if(g.submission_unusable) return fail("timeout poisoned device");
    mode=GOOD;
    if(rme_vk_gpu_submission_wait(&s,100)!=RME_VK_SUBMISSION_OK || s.vk_result!=VK_SUCCESS || waits!=2) return fail("completion");
    if(rme_vk_gpu_submission_wait(&s,100)!=RME_VK_SUBMISSION_OK || waits!=2) return fail("double wait");
    if(rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE || rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK || destroys!=1 || g.active_submissions) return fail("fence release");
    if(rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK || destroys!=1 || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK || device_destroys!=1) return fail("double release");
    reset(GOOD,&g,&s);
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&s)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK) return fail("empty submit");
    enum mode fails[]={CREATE_FAIL,NULL_FENCE};
    for(unsigned i=0;i<2;i++) {
        reset(fails[i],&g,&s);
        if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_FENCE_CREATE_FAILED || submits || destroys || g.active_submissions ||
           s.vk_result!=(i==0?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS)) return fail("fence create failure");
    }
    const char *names[]={"vkGetDeviceProcAddr","vkCreateFence","vkDestroyFence","vkQueueSubmit","vkWaitForFences"};
    for(unsigned i=0;i<5;i++) {
        reset(GOOD,&g,&s); missing=names[i];
        if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_FUNCTION_UNAVAILABLE || creates || submits || destroys || g.active_submissions) return fail("missing entrypoint");
    }
    enum mode failed_submit[]={SUBMIT_OOM,SUBMIT_LOST};
    for(unsigned i=0;i<2;i++) {
        reset(failed_submit[i],&g,&s);
        if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_SUBMIT_FAILED ||
           s.vk_result!=(i==0?VK_ERROR_OUT_OF_HOST_MEMORY:VK_ERROR_DEVICE_LOST) ||
           rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_OUTCOME_UNKNOWN || waits ||
           rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OUTCOME_UNKNOWN ||
           rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE || destroys || device_destroys) return fail("failed submit unknown lifetime");
        struct rme_vk_gpu_submission other={0};
        if(!g.submission_unusable || g.submission_error!=s.vk_result || rme_vk_gpu_submit(instance,get_proc,&g,command,&other)!=RME_VK_SUBMISSION_DEVICE_UNAVAILABLE || submits!=1) return fail("failed submit blocks new work");
    }
    reset(GOOD,&g,&s);
    struct rme_vk_gpu_submission second={0}, third={0};
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&s)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&second)!=RME_VK_SUBMISSION_OK) return fail("multiple pending setup");
    mode=WAIT_ERROR;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_WAIT_FAILED || s.vk_result!=VK_ERROR_OUT_OF_HOST_MEMORY ||
       s.phase!=RME_VK_SUBMISSION_PENDING || g.submission_unusable ||
       rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_STILL_PENDING || destroys ||
       rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE) return fail("wait OOM remains pending and retryable");
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&third)!=RME_VK_SUBMISSION_OK) return fail("wait OOM permits new work");
    mode=WAIT_DEVICE_OOM;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_WAIT_FAILED || s.vk_result!=VK_ERROR_OUT_OF_DEVICE_MEMORY ||
       s.phase!=RME_VK_SUBMISSION_PENDING || g.submission_unusable) return fail("wait device OOM remains pending");
    mode=GOOD;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK) return fail("wait OOM retry succeeds");
    mode=WAIT_LOST;
    if(rme_vk_gpu_submission_wait(&second,0)!=RME_VK_SUBMISSION_WAIT_FAILED || second.vk_result!=VK_ERROR_DEVICE_LOST ||
       second.phase!=RME_VK_SUBMISSION_LOST_COMPLETE || !g.submission_unusable || g.submission_error!=VK_ERROR_DEVICE_LOST ||
       rme_vk_gpu_submission_wait(&second,0)!=RME_VK_SUBMISSION_WAIT_FAILED || second.vk_result!=VK_ERROR_DEVICE_LOST ||
       rme_vk_gpu_submission_dispose(&second)!=RME_VK_SUBMISSION_OK || destroys!=2 ||
       rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE) return fail("lost wait releases only covered fence");
    struct rme_vk_gpu_submission blocked={0};
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&blocked)!=RME_VK_SUBMISSION_DEVICE_UNAVAILABLE || blocked.vk_result!=VK_ERROR_DEVICE_LOST) return fail("lost wait blocks new work");
    if(rme_vk_gpu_submission_wait(&third,0)!=RME_VK_SUBMISSION_WAIT_FAILED || rme_vk_gpu_submission_dispose(&third)!=RME_VK_SUBMISSION_OK ||
       destroys!=3 || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK || device_destroys!=1) return fail("other pending must be separately waited");
    reset(GOOD,&g,&s); memset(&second,0,sizeof(second));
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&s)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&second)!=RME_VK_SUBMISSION_OK) return fail("upgrade setup");
    mode=SUBMIT_OOM; memset(&third,0,sizeof(third));
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&third)!=RME_VK_SUBMISSION_SUBMIT_FAILED || g.submission_error!=VK_ERROR_OUT_OF_HOST_MEMORY) return fail("upgrade initial error");
    mode=WAIT_LOST;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_WAIT_FAILED || g.submission_error!=VK_ERROR_DEVICE_LOST || rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submission_dispose(&second)!=RME_VK_SUBMISSION_STILL_PENDING || rme_vk_gpu_submission_dispose(&third)!=RME_VK_SUBMISSION_OUTCOME_UNKNOWN || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE) return fail("device lost upgrades earlier failure");
    reset(GOOD,&g,&s); memset(&second,0,sizeof(second)); memset(&third,0,sizeof(third));
    if(rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&s)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&second)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submit(instance,get_proc,&g,VK_NULL_HANDLE,&third)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submission_wait(&third,0)!=RME_VK_SUBMISSION_OK) return fail("lost versus success setup");
    mode=WAIT_LOST;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_WAIT_FAILED) return fail("lost versus success loss");
    mode=GOOD;
    if(rme_vk_gpu_submission_wait(&second,0)!=RME_VK_SUBMISSION_WAIT_FAILED || second.vk_result!=VK_SUCCESS || second.phase!=RME_VK_SUBMISSION_LOST_COMPLETE ||
       rme_vk_gpu_submission_wait(&third,0)!=RME_VK_SUBMISSION_WAIT_FAILED || third.vk_result!=VK_SUCCESS || third.phase!=RME_VK_SUBMISSION_LOST_COMPLETE) return fail("lost device must not report success");
    if(rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_submission_dispose(&second)!=RME_VK_SUBMISSION_OK ||
       rme_vk_gpu_submission_dispose(&third)!=RME_VK_SUBMISSION_OK || rme_vk_gpu_close(&g)!=RME_VK_DEVICE_OK) return fail("lost success cleanup");
    reset(GOOD,&g,&s);
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_OK) return fail("unexpected wait error setup");
    mode=WAIT_OTHER;
    if(rme_vk_gpu_submission_wait(&s,0)!=RME_VK_SUBMISSION_WAIT_FAILED || s.vk_result!=VK_ERROR_UNKNOWN ||
       s.phase!=RME_VK_SUBMISSION_UNKNOWN || !g.submission_unusable ||
       rme_vk_gpu_submission_dispose(&s)!=RME_VK_SUBMISSION_OUTCOME_UNKNOWN ||
       rme_vk_gpu_close(&g)!=RME_VK_DEVICE_SUBMISSIONS_ACTIVE) return fail("unexpected wait error stays unknown");
    /* DEVICE_LOST is not a listed vkCreateFence result; defensive nonconforming-driver case only. */
    reset(CREATE_LOST,&g,&s);
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&s)!=RME_VK_SUBMISSION_FENCE_CREATE_FAILED || s.vk_result!=VK_ERROR_DEVICE_LOST || !g.submission_unusable || g.submission_error!=VK_ERROR_DEVICE_LOST || submits) return fail("create fence device lost");
    struct rme_vk_gpu_submission other={0};
    if(rme_vk_gpu_submit(instance,get_proc,&g,command,&other)!=RME_VK_SUBMISSION_DEVICE_UNAVAILABLE) return fail("fence loss blocks new work");
    puts("FAKE_VULKAN_SUBMISSION_LIFECYCLE_OK (fake devices, not a GPU)"); return 0;
}
