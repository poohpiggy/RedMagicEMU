// CPU-only test of extracted upstream acquire-fence control flow.
// The fake descriptor is an integer; no real fence, device or mapping is used.
#include <cassert>
enum VkResult { VK_SUCCESS = 0, VK_ERROR_DEVICE_LOST = -4 };
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define AEMU_SCOPED_TRACE(...) ((void)0)
struct Sync {
    int wait_result = 0, waits = 0, closes = 0, last_fd = -1, timeout = 0;
    int wait(int fd, int ms) { waits++; last_fd = fd; timeout = ms; return wait_result; }
    int close(int fd) { assert(fd == last_fd); closes++; return 0; }
};
struct Connection { Sync sync; Sync *syncHelper() { return &sync; } };
static Connection connection;
static Connection *get_connection() { return &connection; }
struct ResourceTracker {
    struct Callbacks { Connection *(*hostConnectionGetFunc)(); };
    static Callbacks threadingCallbacks;
    @RETURN_TYPE@ unwrap_vkAcquireImageANDROID_nativeFenceFd(int, int*);
};
ResourceTracker::Callbacks ResourceTracker::threadingCallbacks = { get_connection };
@HELPER@
static ResourceTracker tracker;
static ResourceTracker *sResourceTracker = &tracker;
struct FakeEncoder {
    int locks = 0, unlocks = 0, packets = 0, sent_fd = -2;
    void lock() { locks++; }
    void unlock() { unlocks++; }
    VkResult acquire(int nativeFenceFd, bool doLock, bool queueSubmitWithCommandsEnabled) {
        if (!queueSubmitWithCommandsEnabled && doLock) this->lock();
        int local_nativeFenceFd = nativeFenceFd;
        @CHECKPOINT@
        sent_fd = local_nativeFenceFd;
        packets++;
        if (!queueSubmitWithCommandsEnabled && doLock) this->unlock();
        return VK_SUCCESS;
    }
};
int main() {
    FakeEncoder no_fence;
    assert(no_fence.acquire(-1, true, false) == VK_SUCCESS);
    assert(connection.sync.waits == 0 && connection.sync.closes == 0 && no_fence.packets == 1);
    FakeEncoder passed;
    assert(passed.acquire(7, true, false) == VK_SUCCESS);
    assert(connection.sync.waits == 1 && connection.sync.closes == 1 && connection.sync.timeout == 3000);
    assert(passed.locks == 1 && passed.unlocks == 1 && passed.packets == 1);
    connection.sync.wait_result = -1;
    FakeEncoder failed;
    assert(failed.acquire(8, true, false) == VK_ERROR_DEVICE_LOST);
    assert(connection.sync.waits == 2 && connection.sync.closes == 2);
    assert(failed.locks == 1 && failed.unlocks == 1 && failed.packets == 0);
    FakeEncoder no_lock;
    assert(no_lock.acquire(9, true, true) == VK_ERROR_DEVICE_LOST);
    assert(no_lock.locks == 0 && no_lock.unlocks == 0 && no_lock.packets == 0);
    assert(passed.sent_fd == -1);
}
