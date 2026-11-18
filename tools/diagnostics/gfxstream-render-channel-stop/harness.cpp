// Models the real channel's start-on-construction and wait-on-destruction.
// The method under test is extracted from the supplied gfxstream source.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace android { namespace base {
struct Stream {};
class AutoLock {
 public:
  explicit AutoLock(std::mutex& m) : lock(m) {}
 private:
  std::lock_guard<std::mutex> lock;
};
} }

static std::atomic<int> constructions{0};
static std::mutex gate_mutex;
static std::condition_variable gate_cv;
static bool hold_constructor = false, constructor_entered = false, release_constructor = false;

struct FakeThread {
  bool finished = false;
  bool isFinished() const { return finished; }
  void wait() {}
};

class RenderChannelImpl {
 public:
  RenderChannelImpl(android::base::Stream*, uint32_t) {
    ++constructions;
    std::unique_lock<std::mutex> lock(gate_mutex);
    if (hold_constructor) {
      constructor_entered = true;
      gate_cv.notify_all();
      gate_cv.wait(lock, [] { return release_constructor; });
    }
  }
  ~RenderChannelImpl() {
    // Real RenderChannelImpl::~RenderChannelImpl waits on its render thread,
    // which is blocked on channel input until stop() closes the queue.
    std::unique_lock<std::mutex> lock(mu);
    cv.wait(lock, [this] { return stopped; });
  }
  void stopFromHost() {
    std::lock_guard<std::mutex> lock(mu);
    stopped = true;
    thread.finished = true;
    cv.notify_all();
  }
  FakeThread* renderThread() { return &thread; }
 private:
  std::mutex mu;
  std::condition_variable cv;
  bool stopped = false;
  FakeThread thread;
};
using RenderChannelPtr = std::shared_ptr<RenderChannelImpl>;
#define GL_LOG(...) ((void)0)

class RendererImpl {
 public:
  RenderChannelPtr createRenderChannel(android::base::Stream* loadStream,
                                       uint32_t virtioGpuContextId);
  void stopForTest() {
    std::vector<RenderChannelPtr> channels;
    {
      std::lock_guard<std::mutex> lock(mChannelsLock);
      mStopped = true;
      channels = mChannels;
    }
    for (const auto& c : channels) c->stopFromHost();
  }
  std::mutex mChannelsLock;
  bool mStopped = false;
  std::vector<RenderChannelPtr> mChannels;
  std::unique_ptr<FakeThread> mLoaderRenderThread;
};

#include "method.h"

static int already_stopped() {
  RendererImpl r;
  r.mStopped = true;
  auto channel = r.createRenderChannel(nullptr, 1);
  return channel || constructions != 0 ? 1 : 0;
}

static int normal() {
  RendererImpl r;
  auto first = r.createRenderChannel(nullptr, 1);
  if (!first || r.mChannels.size() != 1 || constructions != 1) return 1;
  first->stopFromHost();
  auto second = r.createRenderChannel(nullptr, 2);
  if (!second || r.mChannels.size() != 1 || constructions != 2) return 2;
  second->stopFromHost();
  return 0;
}

static int stopped_during_construction() {
  RendererImpl r;
  RenderChannelPtr channel;
  {
    std::lock_guard<std::mutex> lock(gate_mutex);
    hold_constructor = true;
  }
  std::thread creator([&] { channel = r.createRenderChannel(nullptr, 1); });
  {
    std::unique_lock<std::mutex> lock(gate_mutex);
    if (!gate_cv.wait_for(lock, std::chrono::seconds(1), [] { return constructor_entered; }))
      std::terminate();
  }
  std::atomic<bool> stop_started{false};
  std::thread stopper([&] { stop_started = true; r.stopForTest(); });
  while (!stop_started) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  {
    std::lock_guard<std::mutex> lock(gate_mutex);
    release_constructor = true;
  }
  gate_cv.notify_all();
  creator.join();
  stopper.join();
  return !channel || !r.mStopped ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  if (!std::strcmp(argv[1], "stopped")) return already_stopped();
  if (!std::strcmp(argv[1], "normal")) return normal();
  if (!std::strcmp(argv[1], "race")) return stopped_during_construction();
  return 2;
}
