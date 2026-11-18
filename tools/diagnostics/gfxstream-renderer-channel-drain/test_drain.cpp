#include "channel.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace android { namespace base {
class AutoLock {
public:
    explicit AutoLock(std::mutex& m) : lock_(m) {}
    void unlock() { lock_.unlock(); }
private:
    std::unique_lock<std::mutex> lock_;
};
} }

struct FakeWorker {
    std::mutex lock;
    std::condition_variable cv;
    bool stop = false, release = false;
    std::atomic<bool> exited{false};
    std::thread thread;
    FakeWorker() : thread([this] {
        std::unique_lock<std::mutex> guard(lock);
        cv.wait(guard, [this] { return stop && release; });
        exited = true;
    }) {}
    ~FakeWorker() { if (thread.joinable()) thread.join(); }
    void wait() { if (thread.joinable()) thread.join(); }
};
struct RenderChannelImpl {
    FakeWorker worker;
    void stopFromHost() {
        std::lock_guard<std::mutex> guard(worker.lock);
        worker.stop = true;
        worker.cv.notify_all();
    }
    FakeWorker* renderThread() { return &worker; }
};
struct RendererImpl {
    std::mutex mChannelsLock;
    bool mStopped = false;
    std::vector<std::shared_ptr<RenderChannelImpl>> mChannels;
    void rmeDrainForSession();
};
#ifdef RME_CANDIDATE
#include "drain_method.h"
#else
void RendererImpl::rmeDrainForSession() {}
#endif

int main() {
    rme::Device device;
    if (!device.initialize()) return 1;
    auto pipe = device.open(rme::Service::Process);
    if (!pipe) return 2;
    int32_t confirm = 100;
    uint64_t id = 0;
    if (pipe->send(&confirm, sizeof(confirm)) != sizeof(confirm) ||
        pipe->receive(&id, sizeof(id)) != sizeof(id) || !id) return 3;
    RendererImpl renderer;
    auto channel = std::make_shared<RenderChannelImpl>();
    renderer.mChannels.push_back(channel);
    std::atomic<bool> drained{false};
    std::thread drainer([&] { renderer.rmeDrainForSession(); drained = true; });
    bool stopped;
    {
        std::unique_lock<std::mutex> guard(channel->worker.lock);
        stopped = channel->worker.cv.wait_for(guard, std::chrono::seconds(1),
                                              [&] { return channel->worker.stop; });
    }
    const bool returnedBeforeExit = drained.load();
    {
        std::lock_guard<std::mutex> guard(channel->worker.lock);
        channel->worker.stop = true;
        channel->worker.release = true;
        channel->worker.cv.notify_all();
    }
    drainer.join();
    channel->worker.wait();
    if (!stopped || returnedBeforeExit || !channel->worker.exited || !renderer.mStopped) {
        fprintf(stderr, "FAIL: renderer returned before worker was stopped and joined\n");
        return 4;
    }
    pipe.reset();
    puts("PASS: worker drained before actual GLProcessPipe closed");
}
