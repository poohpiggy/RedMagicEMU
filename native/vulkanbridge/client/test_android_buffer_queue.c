/* CPU-only ownership and error-path test; integers are not real fence fds. */
#include "android_buffer_queue.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>

static int timeout_rc, dequeue_rc, queue_rc, cancel_rc;
static int set_calls, dequeue_calls, queue_calls, cancel_calls, consumed_fd;
static int64_t timeout_seen;
static int set_timeout(struct ANativeWindow *w, int64_t timeout) { assert(w); set_calls++; timeout_seen = timeout; return timeout_rc; }
static int dequeue(struct ANativeWindow *w, struct ANativeWindowBuffer **b, int *fd) {
    assert(w); dequeue_calls++; if (dequeue_rc) return dequeue_rc;
    *b = (struct ANativeWindowBuffer *)2; *fd = 17; return 0;
}
static int queue(struct ANativeWindow *w, struct ANativeWindowBuffer *b, int fd) {
    assert(w && b); queue_calls++; consumed_fd = fd; return queue_rc;
}
static int cancel(struct ANativeWindow *w, struct ANativeWindowBuffer *b, int fd) {
    assert(w && b); cancel_calls++; consumed_fd = fd; return cancel_rc;
}
static const struct RmeAndroidQueueOps ops = {set_timeout, dequeue, queue, cancel};
int main(void) {
    struct ANativeWindow *window = (struct ANativeWindow *)1;
    struct ANativeWindowBuffer *buffer = NULL;
    int fd = -1;
    timeout_rc = -1;
    assert(rme_android_dequeue(window, &ops, 20, &buffer, &fd) == VK_ERROR_SURFACE_LOST_KHR);
    assert(set_calls == 1 && dequeue_calls == 0);
    timeout_rc = 0; dequeue_rc = -ETIMEDOUT;
    assert(rme_android_dequeue(window, &ops, 0, &buffer, &fd) == VK_NOT_READY);
    assert(rme_android_dequeue(window, &ops, 3, &buffer, &fd) == VK_TIMEOUT);
    dequeue_rc = -EIO;
    assert(rme_android_dequeue(window, &ops, 1, &buffer, &fd) == VK_ERROR_SURFACE_LOST_KHR);
    dequeue_rc = 0;
    assert(rme_android_dequeue(window, &ops, UINT64_MAX, &buffer, &fd) == VK_SUCCESS);
    assert(buffer == (struct ANativeWindowBuffer *)2 && fd == 17 && timeout_seen == -1);
    queue_rc = -EIO;
    assert(rme_android_queue(window, &ops, buffer, fd) == VK_ERROR_SURFACE_LOST_KHR);
    assert(queue_calls == 1 && consumed_fd == 17);
    cancel_rc = -EIO;
    assert(rme_android_cancel(window, &ops, buffer, 18) == VK_ERROR_SURFACE_LOST_KHR);
    assert(cancel_calls == 1 && consumed_fd == 18);
}
