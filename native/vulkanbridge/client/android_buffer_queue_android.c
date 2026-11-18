/* Direct Android native-window callbacks; no device access or sync emulation. */
#include "android_buffer_queue.h"
#include "android_swapchain_slots.h"
#include <system/window.h>

static int set_timeout(struct ANativeWindow *window, int64_t timeout) {
    return window->perform(window, NATIVE_WINDOW_SET_DEQUEUE_TIMEOUT, timeout);
}
static int dequeue(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer, int *fence) {
    return window->dequeueBuffer(window, buffer, fence);
}
static int queue(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fence) {
    return window->queueBuffer(window, buffer, fence);
}
static int cancel(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fence) {
    return window->cancelBuffer(window, buffer, fence);
}
const struct RmeAndroidQueueOps *rme_android_queue_ops(void) {
    static const struct RmeAndroidQueueOps ops = {set_timeout, dequeue, queue, cancel};
    return &ops;
}
static void retain_buffer(struct ANativeWindowBuffer *buffer) {
    buffer->common.incRef(&buffer->common);
}
static void release_buffer(struct ANativeWindowBuffer *buffer) {
    buffer->common.decRef(&buffer->common);
}
const struct RmeAndroidBufferRefs *rme_android_buffer_refs(void) {
    static const struct RmeAndroidBufferRefs refs = {retain_buffer, release_buffer};
    return &refs;
}
