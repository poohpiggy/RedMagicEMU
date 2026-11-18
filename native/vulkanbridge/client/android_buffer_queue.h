/* Internal native buffer-queue operations; this is not a Vulkan swapchain. */
#ifndef RME_ANDROID_BUFFER_QUEUE_H
#define RME_ANDROID_BUFFER_QUEUE_H
#include <stdint.h>
#include <vulkan/vulkan_core.h>

struct ANativeWindow;
struct ANativeWindowBuffer;
struct RmeAndroidQueueOps {
    int (*set_timeout)(struct ANativeWindow *, int64_t);
    int (*dequeue)(struct ANativeWindow *, struct ANativeWindowBuffer **, int *);
    int (*queue)(struct ANativeWindow *, struct ANativeWindowBuffer *, int);
    int (*cancel)(struct ANativeWindow *, struct ANativeWindowBuffer *, int);
};

VkResult rme_android_dequeue(struct ANativeWindow *, const struct RmeAndroidQueueOps *,
    uint64_t timeout, struct ANativeWindowBuffer **, int *fence_fd);
/* queue/cancel transfer ownership of fence_fd to ANativeWindow even on error. */
VkResult rme_android_queue(struct ANativeWindow *, const struct RmeAndroidQueueOps *,
    struct ANativeWindowBuffer *, int fence_fd);
VkResult rme_android_cancel(struct ANativeWindow *, const struct RmeAndroidQueueOps *,
    struct ANativeWindowBuffer *, int fence_fd);
const struct RmeAndroidQueueOps *rme_android_queue_ops(void);
#endif
