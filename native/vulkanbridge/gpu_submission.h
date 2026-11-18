/* Explicit Vulkan submit/fence lifetime, with no implicit data transfer. */
#ifndef RME_VULKAN_GPU_SUBMISSION_H
#define RME_VULKAN_GPU_SUBMISSION_H
#include "gpu_device.h"

enum rme_vk_submission_status {
    RME_VK_SUBMISSION_OK = 0,
    RME_VK_SUBMISSION_INVALID_ARGUMENT,
    RME_VK_SUBMISSION_ALREADY_ACTIVE,
    RME_VK_SUBMISSION_FUNCTION_UNAVAILABLE,
    RME_VK_SUBMISSION_FENCE_CREATE_FAILED,
    RME_VK_SUBMISSION_SUBMIT_FAILED,
    RME_VK_SUBMISSION_TIMEOUT,
    RME_VK_SUBMISSION_WAIT_FAILED,
    RME_VK_SUBMISSION_STILL_PENDING,
    RME_VK_SUBMISSION_OUTCOME_UNKNOWN,
    RME_VK_SUBMISSION_LIMIT_EXCEEDED,
    RME_VK_SUBMISSION_INVALID_STATE,
    RME_VK_SUBMISSION_DEVICE_UNAVAILABLE
};
enum rme_vk_submission_phase {
    RME_VK_SUBMISSION_EMPTY = 0,
    RME_VK_SUBMISSION_PENDING,
    RME_VK_SUBMISSION_COMPLETE,
    RME_VK_SUBMISSION_LOST_COMPLETE,
    RME_VK_SUBMISSION_UNKNOWN
};
struct rme_vk_gpu_submission {
    enum rme_vk_submission_status status;
    enum rme_vk_submission_phase phase;
    VkResult vk_result;
    struct rme_vk_gpu_device *owner;
    VkFence fence;
    PFN_vkWaitForFences wait;
    PFN_vkDestroyFence destroy;
};
/* State must initially be zeroed and is an exclusive owner: do not copy or
 * manually zero it while active. The instance and gpu must outlive the state.
 * Caller externally serializes device/queue access and guarantees that any
 * non-null command buffer is executable, belongs to gpu, and all command
 * buffers, pools and referenced resources remain alive until completion. On an
 * unknown outcome they must remain alive; this API provides no recovery.
 * Null command requests a real empty submission with a fence. */
enum rme_vk_submission_status rme_vk_gpu_submit(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    VkCommandBuffer command, struct rme_vk_gpu_submission *state);
enum rme_vk_submission_status rme_vk_gpu_submission_wait(struct rme_vk_gpu_submission *state, uint64_t timeout_ns);
/* Dispose means fence cleanup only. A lost-complete wait is still a failed
 * execution; unknown and pending submissions cannot be released here. */
enum rme_vk_submission_status rme_vk_gpu_submission_dispose(struct rme_vk_gpu_submission *state);
#endif
