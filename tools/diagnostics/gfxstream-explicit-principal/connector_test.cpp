#include "pipe_principal_registry.h"
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

static rme::PipePrincipalRegistry registry;
static std::weak_ptr<const rme::PipePrincipal> observed;
static bool observed_empty;
static unsigned started;
struct HwPipe {
    const AndroidPipeHwFuncs* funcs;
    static void close(void*) {}
    static void signal(void*, unsigned) {}
    static int id(void*) { return 1; }
    static const AndroidPipeHwFuncs vtable;
};
const AndroidPipeHwFuncs HwPipe::vtable = {HwPipe::close, HwPipe::signal, HwPipe::id};

// Fake renderer/channel: capture the explicit argument before starting worker.
// This does not execute the actual EmuglPipe or Vulkan decoder.
class TestPipe final : public android::AndroidPipe {
public:
    TestPipe(void* hw, Service* svc, std::shared_ptr<const rme::PipePrincipal> principal)
        : AndroidPipe(hw, svc), worker_([this, principal] {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_; });
            (void)principal;
        }) { ++started; }
    void onGuestClose(PipeCloseReason) override {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        cv_.notify_one(); worker_.join(); delete this;
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
    TestService() : Service("rme-explicit-principal-test") {}
    android::AndroidPipe* create(void* hw, const char*, AndroidPipeFlags) override {
        auto principal = registry.lookup(hw);
        observed_empty = !principal;
        observed = principal;
        return new TestPipe(hw, this, std::move(principal));
    }
};
static void* open_pipe(HwPipe& hw) {
    android::ScopedVmLock lock; return android_pipe_guest_open(&hw);
}
static int send_selector(void** pipe) {
    static char selector[] = "pipe:rme-explicit-principal-test";
    AndroidPipeBuffer buffer{reinterpret_cast<uint8_t*>(selector), sizeof(selector)};
    android::ScopedVmLock lock; return android_pipe_guest_send(pipe, &buffer, 1);
}
static void close_pipe(void* pipe) {
    android::ScopedVmLock lock; android_pipe_guest_close(pipe, PIPE_CLOSE_GRACEFUL);
}
int main() {
    rme::Device device;
    assert(device.initialize());
    android::AndroidPipe::Service::add(std::make_unique<TestService>());
    HwPipe hw{&HwPipe::vtable};
    void* legacy = open_pipe(hw);
    assert(legacy && send_selector(&legacy) == sizeof("pipe:rme-explicit-principal-test"));
    assert(observed_empty && started == 1);
    close_pipe(legacy);
    auto principal = std::make_shared<const rme::PipePrincipal>();
    std::weak_ptr<const rme::PipePrincipal> lifetime = principal;
    auto binding = registry.bind(&hw, principal);
    assert(binding);
    void* first = open_pipe(hw);
    assert(first && send_selector(&first) == sizeof("pipe:rme-explicit-principal-test"));
    assert(!observed_empty && observed.lock() == principal && started == 2);
    binding.reset(); principal.reset();
    assert(!registry.lookup(&hw) && !lifetime.expired());
    close_pipe(first);
    assert(lifetime.expired());
    auto next = std::make_shared<const rme::PipePrincipal>();
    binding = registry.bind(&hw, next);
    assert(binding);
    void* second = open_pipe(hw);
    assert(second && send_selector(&second) == sizeof("pipe:rme-explicit-principal-test"));
    assert(observed.lock() == next && started == 3);
    close_pipe(second);
    binding.reset(); next.reset();
    assert(observed.expired());
}
