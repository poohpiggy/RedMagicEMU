#include "principal_scope.h"
#include "channel.h"
#include "host-common/AndroidPipe.h"
#include "host-common/VmLock.h"
#include "host-common/android_pipe_base.h"
#include "host-common/android_pipe_device.h"
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

using rme::Principal;
using rme::PrincipalScope;
static bool fail_creation;
static unsigned attempts, started;
static std::weak_ptr<const Principal> observed;

struct HwPipe {
    const AndroidPipeHwFuncs* funcs;
    static void close(void*) {}
    static void signal(void*, unsigned) {}
    static int id(void*) { return 1; }
    static const AndroidPipeHwFuncs vtable;
};
const AndroidPipeHwFuncs HwPipe::vtable = {HwPipe::close, HwPipe::signal, HwPipe::id};

// A renderer substitute captures before starting its worker, matching the
// relevant RenderChannelImpl construction order, but does no GPU operations.
class TestPipe final : public android::AndroidPipe {
public:
    TestPipe(void* hw, Service* svc, std::shared_ptr<const Principal> principal)
        : AndroidPipe(hw, svc), worker_([this, principal] {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_; });
            assert(principal);
        }) { ++started; }
    void onGuestClose(PipeCloseReason) override {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        cv_.notify_one();
        worker_.join();
        delete this;
    }
    unsigned onGuestPoll() const override { return 0; }
    int onGuestRecv(AndroidPipeBuffer*, int) override { return PIPE_ERROR_AGAIN; }
    int onGuestSend(const AndroidPipeBuffer*, int, void**) override { return PIPE_ERROR_INVAL; }
    void onGuestWantWakeOn(int) override {}
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread worker_;
};
class TestService final : public android::AndroidPipe::Service {
public:
    TestService() : Service("rme-principal-test") {}
    android::AndroidPipe* create(void* hw, const char*, AndroidPipeFlags) override {
        ++attempts;
        auto principal = PrincipalScope::take();
        if (!principal || fail_creation) return nullptr;
        observed = principal;
        return new TestPipe(hw, this, std::move(principal));
    }
};
static void* open_pipe(HwPipe& hw) {
    android::ScopedVmLock lock;
    return android_pipe_guest_open(&hw);
}
static int send_selector(void** pipe) {
    static char selector[] = "pipe:rme-principal-test";
    AndroidPipeBuffer buffer{reinterpret_cast<uint8_t*>(selector), sizeof(selector)};
    android::ScopedVmLock lock;
    return android_pipe_guest_send(pipe, &buffer, 1);
}
static void close_pipe(void* pipe) {
    android::ScopedVmLock lock;
    android_pipe_guest_close(pipe, PIPE_CLOSE_GRACEFUL);
}
int main() {
    rme::Device device;
    assert(device.initialize());
    android::AndroidPipe::Service::add(std::make_unique<TestService>());
    HwPipe hw{&HwPipe::vtable};
    void* missing = open_pipe(hw);
    assert(missing && send_selector(&missing) == PIPE_ERROR_INVAL && started == 0);
    close_pipe(missing);
    auto principal = std::make_shared<const Principal>();
    std::weak_ptr<const Principal> lifetime = principal;
    void* first = open_pipe(hw);
    void* second = open_pipe(hw);
    {
        PrincipalScope scope(principal);
        assert(send_selector(&first) == sizeof("pipe:rme-principal-test"));
        assert(observed.lock() == principal && started == 1);
        assert(send_selector(&second) == PIPE_ERROR_INVAL && started == 1);
    }
    principal.reset();
    assert(!lifetime.expired());
    close_pipe(second);
    close_pipe(first);
    assert(lifetime.expired());
    void* failed = open_pipe(hw);
    principal = std::make_shared<const Principal>();
    fail_creation = true;
    { PrincipalScope scope(principal); assert(send_selector(&failed) == PIPE_ERROR_INVAL); assert(!PrincipalScope::take()); }
    fail_creation = false;
    assert(!PrincipalScope::take());
    close_pipe(failed);
    assert(attempts == 4 && started == 1);
}
