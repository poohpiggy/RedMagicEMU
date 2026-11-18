#include "gpu_submission.h"
#include <string.h>

static void mark_unusable(struct rme_vk_gpu_device *gpu, VkResult result) {
    if (!gpu->submission_unusable || result==VK_ERROR_DEVICE_LOST) { gpu->submission_unusable=VK_TRUE; gpu->submission_error=result; }
}

enum rme_vk_submission_status rme_vk_gpu_submit(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    VkCommandBuffer command, struct rme_vk_gpu_submission *state) {
    if (!state) return RME_VK_SUBMISSION_INVALID_ARGUMENT;
    if (state->phase != RME_VK_SUBMISSION_EMPTY || state->fence)
        return state->status = RME_VK_SUBMISSION_ALREADY_ACTIVE;
    memset(state, 0, sizeof(*state));
    if (!instance || !get_proc || !gpu || !gpu->device || !gpu->queue || !gpu->selected.device)
        return state->status = RME_VK_SUBMISSION_INVALID_ARGUMENT;
    if (gpu->submission_unusable) { state->vk_result=gpu->submission_error; return state->status=RME_VK_SUBMISSION_DEVICE_UNAVAILABLE; }
    if (gpu->active_submissions == UINT32_MAX) return state->status = RME_VK_SUBMISSION_LIMIT_EXCEEDED;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)get_proc(instance, "vkGetDeviceProcAddr");
    if (!gdpa) return state->status = RME_VK_SUBMISSION_FUNCTION_UNAVAILABLE;
    PFN_vkCreateFence create = (PFN_vkCreateFence)gdpa(gpu->device, "vkCreateFence");
    PFN_vkDestroyFence destroy = (PFN_vkDestroyFence)gdpa(gpu->device, "vkDestroyFence");
    PFN_vkQueueSubmit submit = (PFN_vkQueueSubmit)gdpa(gpu->device, "vkQueueSubmit");
    PFN_vkWaitForFences wait = (PFN_vkWaitForFences)gdpa(gpu->device, "vkWaitForFences");
    if (!create || !destroy || !submit || !wait) return state->status = RME_VK_SUBMISSION_FUNCTION_UNAVAILABLE;
    VkFenceCreateInfo fi = { .sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence=VK_NULL_HANDLE;
    state->vk_result=create(gpu->device, &fi, NULL, &fence);
    if (state->vk_result != VK_SUCCESS || !fence) {
        if (state->vk_result==VK_ERROR_DEVICE_LOST) mark_unusable(gpu,state->vk_result);
        return state->status = RME_VK_SUBMISSION_FENCE_CREATE_FAILED;
    }
    state->owner=gpu; state->fence=fence; state->destroy=destroy; state->wait=wait;
    ++gpu->active_submissions;
    VkSubmitInfo info = { .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=command ? 1u : 0u, .pCommandBuffers=command ? &command : NULL };
    state->vk_result=submit(gpu->queue, 1, &info, fence);
    if (state->vk_result != VK_SUCCESS) {
        state->phase=RME_VK_SUBMISSION_UNKNOWN;
        mark_unusable(gpu,state->vk_result);
        return state->status=RME_VK_SUBMISSION_SUBMIT_FAILED;
    }
    state->phase=RME_VK_SUBMISSION_PENDING;
    return state->status=RME_VK_SUBMISSION_OK;
}

static int valid_active(const struct rme_vk_gpu_submission *state) {
    return state->owner && state->owner->device && state->owner->active_submissions &&
        state->fence && state->wait && state->destroy;
}

enum rme_vk_submission_status rme_vk_gpu_submission_wait(struct rme_vk_gpu_submission *state, uint64_t timeout_ns) {
    if (!state) return RME_VK_SUBMISSION_INVALID_ARGUMENT;
    if (!valid_active(state)) return state->status=RME_VK_SUBMISSION_INVALID_STATE;
    if (state->phase==RME_VK_SUBMISSION_UNKNOWN) return state->status=RME_VK_SUBMISSION_OUTCOME_UNKNOWN;
    if (state->phase==RME_VK_SUBMISSION_COMPLETE) {
        if (state->owner->submission_unusable && state->owner->submission_error==VK_ERROR_DEVICE_LOST) {
            state->phase=RME_VK_SUBMISSION_LOST_COMPLETE; return state->status=RME_VK_SUBMISSION_WAIT_FAILED;
        }
        return state->status=RME_VK_SUBMISSION_OK;
    }
    if (state->phase==RME_VK_SUBMISSION_LOST_COMPLETE) return state->status=RME_VK_SUBMISSION_WAIT_FAILED;
    if (state->phase!=RME_VK_SUBMISSION_PENDING) return state->status=RME_VK_SUBMISSION_INVALID_STATE;
    state->vk_result=state->wait(state->owner->device, 1, &state->fence, VK_TRUE, timeout_ns);
    if (state->vk_result==VK_SUCCESS) {
        if (state->owner->submission_unusable && state->owner->submission_error==VK_ERROR_DEVICE_LOST) {
            state->phase=RME_VK_SUBMISSION_LOST_COMPLETE; return state->status=RME_VK_SUBMISSION_WAIT_FAILED;
        }
        state->phase=RME_VK_SUBMISSION_COMPLETE; return state->status=RME_VK_SUBMISSION_OK;
    }
    if (state->vk_result==VK_TIMEOUT) return state->status=RME_VK_SUBMISSION_TIMEOUT;
    if (state->vk_result==VK_ERROR_OUT_OF_HOST_MEMORY || state->vk_result==VK_ERROR_OUT_OF_DEVICE_MEMORY)
        return state->status=RME_VK_SUBMISSION_WAIT_FAILED;
    state->phase=state->vk_result==VK_ERROR_DEVICE_LOST ? RME_VK_SUBMISSION_LOST_COMPLETE : RME_VK_SUBMISSION_UNKNOWN;
    mark_unusable(state->owner,state->vk_result);
    return state->status=RME_VK_SUBMISSION_WAIT_FAILED;
}

enum rme_vk_submission_status rme_vk_gpu_submission_dispose(struct rme_vk_gpu_submission *state) {
    if (!state) return RME_VK_SUBMISSION_INVALID_ARGUMENT;
    if (state->phase==RME_VK_SUBMISSION_EMPTY && !state->fence && !state->owner) return RME_VK_SUBMISSION_OK;
    if (!valid_active(state)) return state->status=RME_VK_SUBMISSION_INVALID_STATE;
    if (state->phase==RME_VK_SUBMISSION_UNKNOWN) return state->status=RME_VK_SUBMISSION_OUTCOME_UNKNOWN;
    if (state->phase==RME_VK_SUBMISSION_PENDING) return state->status=RME_VK_SUBMISSION_STILL_PENDING;
    if (state->phase!=RME_VK_SUBMISSION_COMPLETE && state->phase!=RME_VK_SUBMISSION_LOST_COMPLETE) return state->status=RME_VK_SUBMISSION_INVALID_STATE;
    state->destroy(state->owner->device, state->fence, NULL);
    --state->owner->active_submissions;
    memset(state, 0, sizeof(*state));
    return RME_VK_SUBMISSION_OK;
}
