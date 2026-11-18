#include "ui_dispatcher.h"
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

namespace rme {
struct UiDispatcher::Impl {
    struct Task {
        Callback callback;
        void* data;
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
    };
    mutable std::mutex mutex;
    std::thread::id owner;
    int event = -1;
    bool accepting = false;
    unsigned inFlight = 0;
    std::deque<std::shared_ptr<Task>> queue;
};
UiDispatcher::UiDispatcher() : impl_(new Impl) {}
UiDispatcher::~UiDispatcher() { if (fd() >= 0) std::terminate(); }
bool UiDispatcher::initialize() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->event >= 0) return false;
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) return false;
    impl_->event = fd;
    impl_->accepting = true;
    impl_->owner = std::this_thread::get_id();
    return true;
}
bool UiDispatcher::isUiThread() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->event >= 0 && impl_->owner == std::this_thread::get_id();
}
int UiDispatcher::fd() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->event;
}
bool UiDispatcher::post(Callback callback, void* data, bool wait) {
    if (!callback) return false;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->event < 0 || !impl_->accepting) return false;
    if (impl_->owner == std::this_thread::get_id()) {
        ++impl_->inFlight;
        lock.unlock();
        try { callback(data); } catch (...) {
            lock.lock(); --impl_->inFlight; throw;
        }
        lock.lock(); --impl_->inFlight;
        return true;
    }
    auto task = std::make_shared<Impl::Task>();
    task->callback = callback;
    task->data = data;
    impl_->queue.push_back(task);
    uint64_t value = 1;
    ssize_t result;
    do { result = write(impl_->event, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    if (result != sizeof(value) && !(result < 0 && errno == EAGAIN)) std::terminate();
    lock.unlock();
    if (wait) {
        std::unique_lock<std::mutex> taskLock(task->mutex);
        task->cv.wait(taskLock, [&] { return task->done; });
    }
    return true;
}
bool UiDispatcher::drain() {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->event < 0 || impl_->owner != std::this_thread::get_id()) return false;
    uint64_t count;
    ssize_t result;
    do { result = read(impl_->event, &count, sizeof(count)); } while (result < 0 && errno == EINTR);
    if (result != sizeof(count) && !(result < 0 && errno == EAGAIN)) return false;
    unsigned processed = 0;
    while (!impl_->queue.empty() && processed++ < 64) {
        auto task = impl_->queue.front();
        impl_->queue.pop_front();
        ++impl_->inFlight;
        lock.unlock();
        task->callback(task->data);
        { std::lock_guard<std::mutex> done(task->mutex); task->done = true; }
        task->cv.notify_all();
        lock.lock();
        --impl_->inFlight;
    }
    if (!impl_->queue.empty()) {
        uint64_t value = 1;
        do { result = write(impl_->event, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
        if (result != sizeof(value) && !(result < 0 && errno == EAGAIN)) std::terminate();
    }
    return true;
}
bool UiDispatcher::quiesce() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->event < 0 || impl_->owner != std::this_thread::get_id() ||
        impl_->inFlight || !impl_->queue.empty()) return false;
    impl_->accepting = false;
    return true;
}
bool UiDispatcher::shutdown() {
    if (!quiesce()) return false;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    close(impl_->event);
    impl_->event = -1;
    impl_->owner = {};
    return true;
}
}
