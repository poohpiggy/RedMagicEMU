#include "channel.h"
#include "waker.h"
#include "host-common/AndroidPipe.h"
#include "host-common/VmLock.h"
#include "host-common/android_pipe_base.h"
#include "host-common/android_pipe_device.h"
#include "host-common/opengles-pipe.h"
#include <climits>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <sys/eventfd.h>
#include <unistd.h>

namespace rme {
namespace {
// The process owns this lock until exit, as upstream retains its pointer.
class DeviceLock final : public android::VmLock {
public:
    void lock() override { mutex_.lock(); ++depth_; }
    void unlock() override { --depth_; mutex_.unlock(); }
    bool isLockedBySelf() const override { return depth_ != 0; }
private:
    std::recursive_mutex mutex_;
    static thread_local unsigned depth_;
};
thread_local unsigned DeviceLock::depth_ = 0;
DeviceLock& lock() { static auto* l = new DeviceLock; return *l; }
bool initialized = false;
std::thread::id owner;
int next_id = 0;
struct WakeLoop {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    std::function<void()> callback;
    static void install(void* p, std::function<void()> cb) { static_cast<WakeLoop*>(p)->callback = std::move(cb); }
    static void uninstall(void* p) { static_cast<WakeLoop*>(p)->callback = {}; }
    static void schedule(void* p, uint64_t timeout) {
        if (timeout != 0) std::terminate();
        uint64_t value = 1;
        ssize_t result;
        do { result = write(static_cast<WakeLoop*>(p)->fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
        if (result < 0 && errno == EAGAIN) return; // Already readable.
        if (result != sizeof(value)) std::terminate();
    }
};
WakeLoop& loop() { static auto* l = new WakeLoop; return *l; }

struct HwPipe {
    const AndroidPipeHwFuncs* funcs;
    int id;
    bool closed = false;
    unsigned wake = 0;
    static void close(void* p) { static_cast<HwPipe*>(p)->closed = true; WakeLoop::schedule(&loop(), 0); }
    static void signal(void* p, unsigned flags) { static_cast<HwPipe*>(p)->wake |= flags; WakeLoop::schedule(&loop(), 0); }
    static int getId(void* p) { return static_cast<HwPipe*>(p)->id; }
    static const AndroidPipeHwFuncs vtable;
};
const AndroidPipeHwFuncs HwPipe::vtable = { HwPipe::close, HwPipe::signal, HwPipe::getId };
bool on_owner() { return initialized && owner == std::this_thread::get_id(); }
}

struct Device::Impl { bool active = false; };
struct Session::Impl {
    HwPipe hw{&HwPipe::vtable, ++next_id};
    void* pipe = nullptr;
    Service service;
    bool confirmed = false;
    bool received = false;
    explicit Impl(Service s) : service(s) {}
};

Device::Device() : impl_(new Impl) {}
Device::~Device() = default;
bool Device::initialize() {
    if (initialized || android::VmLock::hasInstance()) return false;
    if (loop().fd < 0) return false;
    owner = std::this_thread::get_id();
    android::VmLock::set(&lock());
    android::ScopedVmLock guard(&lock());
    android::RmePipeTimer timer{&loop(), WakeLoop::install, WakeLoop::uninstall, WakeLoop::schedule};
    android::rmeInitPipeWaker(&lock(), timer);
    android_init_opengles_pipe();
    initialized = true;
    impl_->active = true;
    return true;
}
int Device::wakeupFd() const {
    return impl_->active && on_owner() ? loop().fd : -1;
}
bool Device::dispatchWakeups() {
    if (!impl_->active || !on_owner()) return false;
    uint64_t value;
    ssize_t result;
    do { result = read(loop().fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    if (result < 0 && errno == EAGAIN) return true;
    if (result != sizeof(value) || !loop().callback) return false;
    android::ScopedVmLock guard(&lock());
    loop().callback();
    return true;
}
std::unique_ptr<Session> Device::open(Service service) {
    if (!impl_->active || !on_owner() || (service != Service::OpenGLES && service != Service::Process)) return {};
    android::ScopedVmLock guard(&lock());
    auto p = std::make_unique<Session::Impl>(service);
    p->pipe = android_pipe_guest_open(&p->hw);
    if (!p->pipe) return {};
    const char* name = service == Service::Process ? "pipe:GLProcessPipe" : "pipe:opengles";
    const size_t n = std::strlen(name) + 1;
    AndroidPipeBuffer b{reinterpret_cast<uint8_t*>(const_cast<char*>(name)), n};
    int result = android_pipe_guest_send(&p->pipe, &b, 1);
    if (result != static_cast<int>(n)) {
        if (p->pipe) android_pipe_guest_close(p->pipe, PIPE_CLOSE_ERROR);
        return {};
    }
    return std::unique_ptr<Session>(new Session(std::move(p)));
}
Session::Session(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
Session::~Session() {
    if (!on_owner()) std::terminate();
    android::ScopedVmLock guard(&lock());
    if (impl_->pipe) android_pipe_guest_close(impl_->pipe, PIPE_CLOSE_GRACEFUL);
}
int Session::send(const void* data, size_t size) {
    if (!on_owner() || !data || !size || size > INT_MAX || impl_->hw.closed) return PIPE_ERROR_INVAL;
    int32_t processConfirm = 0;
    if (impl_->service == Service::Process) {
        if (impl_->confirmed || size != sizeof(processConfirm)) return PIPE_ERROR_INVAL;
        std::memcpy(&processConfirm, data, sizeof(processConfirm));
        if (processConfirm != 100) return PIPE_ERROR_INVAL;
        data = &processConfirm;
    }
    android::ScopedVmLock guard(&lock());
    AndroidPipeBuffer b{static_cast<uint8_t*>(const_cast<void*>(data)), size};
    int result = android_pipe_guest_send(&impl_->pipe, &b, 1);
    if (impl_->service == Service::Process && result == sizeof(int32_t)) impl_->confirmed = true;
    return result;
}
int Session::receive(void* data, size_t size) {
    if (!on_owner() || !data || !size || size > INT_MAX || impl_->hw.closed) return PIPE_ERROR_INVAL;
    if (impl_->service == Service::Process && (!impl_->confirmed || impl_->received || size < sizeof(uint64_t))) return PIPE_ERROR_INVAL;
    android::ScopedVmLock guard(&lock());
    AndroidPipeBuffer b{static_cast<uint8_t*>(data), size};
    int result = android_pipe_guest_recv(impl_->pipe, &b, 1);
    if (impl_->service == Service::Process && result == sizeof(uint64_t)) impl_->received = true;
    return result;
}
unsigned Session::poll() {
    if (!on_owner() || impl_->hw.closed) return PIPE_POLL_HUP;
    android::ScopedVmLock guard(&lock());
    return android_pipe_guest_poll(impl_->pipe);
}
bool Session::watch(unsigned flags) {
    if (!on_owner() || impl_->hw.closed || flags & ~(PIPE_WAKE_READ | PIPE_WAKE_WRITE)) return false;
    android::ScopedVmLock guard(&lock());
    android_pipe_guest_wake_on(impl_->pipe, flags);
    return true;
}
}
