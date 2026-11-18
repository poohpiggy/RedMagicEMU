#include "waker.h"
#include "host-common/AndroidPipe.h"
#include "host-common/android_pipe_base.h"
#include "host-common/android_pipe_host.h"
#include "host-common/VmLock.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

class Lock final : public android::VmLock {
public:
    void lock() override { mu.lock(); ++depth; }
    void unlock() override { --depth; mu.unlock(); }
    bool isLockedBySelf() const override { return depth != 0; }
private:
    std::recursive_mutex mu;
    static thread_local unsigned depth;
};
thread_local unsigned Lock::depth = 0;
struct Loop {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    std::function<void()> callback;
    static void install(void* p, std::function<void()> cb) { static_cast<Loop*>(p)->callback = std::move(cb); }
    static void uninstall(void* p) { static_cast<Loop*>(p)->callback = {}; }
    static void schedule(void* p, uint64_t delay) {
        assert(delay == 0);
        uint64_t n = 1;
        auto* loop = static_cast<Loop*>(p);
        assert(write(loop->fd, &n, sizeof(n)) == sizeof(n));
    }
};
struct Hardware {
    const AndroidPipeHwFuncs* funcs;
    unsigned received = 0;
    std::thread::id callback_thread;
    Lock* lock;
    static void close(void*) { assert(false); }
    static void wake(void* p, unsigned flags) {
        auto* hw = static_cast<Hardware*>(p);
        assert(hw->lock->isLockedBySelf());
        hw->received |= flags;
        hw->callback_thread = std::this_thread::get_id();
    }
    static int id(void*) { return 7; }
    static const AndroidPipeHwFuncs vtable;
};
const AndroidPipeHwFuncs Hardware::vtable = {Hardware::close, Hardware::wake, Hardware::id};
int main() {
    // Process-global runner retains both objects until process exit.
    auto* lock = new Lock;
    auto* loop = new Loop;
    assert(loop->fd >= 0);
    android::VmLock::set(lock);
#ifdef BASELINE
    android::AndroidPipe::initThreading(lock);
#else
    android::RmePipeTimer timer{loop, Loop::install, Loop::uninstall, Loop::schedule};
    android::rmeInitPipeWaker(lock, timer);
#endif
    Hardware hw{&Hardware::vtable, 0, {}, lock};
    std::thread worker([&] { android_pipe_host_signal_wake(&hw, PIPE_WAKE_READ); });
    worker.join();
    struct pollfd pfd{loop->fd, POLLIN, 0};
    if (poll(&pfd, 1, 100) != 1) return 1;
    uint64_t count = 0;
    assert(read(loop->fd, &count, sizeof(count)) == sizeof(count));
    {
        android::ScopedVmLock guard(lock);
        assert(loop->callback);
        loop->callback();
    }
    assert(hw.received == PIPE_WAKE_READ);
    assert(hw.callback_thread == std::this_thread::get_id());
}
