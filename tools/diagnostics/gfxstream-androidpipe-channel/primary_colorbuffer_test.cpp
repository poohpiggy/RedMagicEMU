#include "primary_display.h"
#include "host_color_registry.h"
#include "renderer_start.h"
#include "host-common/multi_display_agent.h"
#include <cassert>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <cstdio>
#include <thread>

// Test substitute around the exact fixed-source FrameBuffer::findColorBuffer
// body, extracted by verify.sh. It does not allocate a real GPU resource.
using HandleType = uint32_t;
using ColorBufferPtr = std::shared_ptr<void>;
using AutoLock = std::lock_guard<std::mutex>;
class FrameBuffer {
public:
    struct Entry { ColorBufferPtr cb; };
    using ColorBufferMap = std::map<HandleType, Entry>;
    ColorBufferPtr findColorBuffer(HandleType p_colorbuffer);
    void add(HandleType h, ColorBufferPtr value) { std::lock_guard<std::mutex> l(m_colorBufferMapLock); m_colorbuffers[h] = {value}; }
    void erase(HandleType h) { std::lock_guard<std::mutex> l(m_colorBufferMapLock); m_colorbuffers.erase(h); }
private:
    std::mutex m_colorBufferMapLock;
    ColorBufferMap m_colorbuffers;
};
#include "framebuffer_find.inc"
static FrameBuffer registry;
static int disappearingLookups = 0;
static std::mutex testMutex;
static std::condition_variable testCv;
static bool shouldBlock = false, blockedLookup = false, releaseLookup = false;
namespace rme {
std::shared_ptr<void> findRegisteredColorBuffer(uint32_t handle) {
    uint32_t w = 0;
    // A lookup must not be made while the agent's own mutex is held.
    assert(primaryDisplayAgent()->getMultiDisplay(0, nullptr, nullptr, &w, nullptr, nullptr, nullptr, nullptr));
    if (handle == 8 && shouldBlock) {
        std::unique_lock<std::mutex> lock(testMutex);
        blockedLookup = true;
        testCv.notify_all();
        testCv.wait(lock, [] { return releaseLookup; });
    }
    if (handle == 7 && ++disappearingLookups == 2) registry.erase(7);
    return registry.findColorBuffer(handle);
}
}
struct Object {
    ~Object() {
        uint32_t width = 0;
        // A replaced reference must not be destroyed under the agent mutex.
        rme::primaryDisplayAgent()->getMultiDisplay(0, nullptr, nullptr, &width, nullptr, nullptr, nullptr, nullptr);
    }
};
int main() {
    auto token = rme::bindPrimaryDisplay(1, 640, 480, 160, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    assert(token && !rme::primaryDisplayAgent()->setDisplayColorBuffer);
    assert(!rme::enablePrimaryColorBuffers(token, 2));
    assert(rme::enablePrimaryColorBuffers(token, 1));
    const auto* agent = rme::primaryDisplayAgent();
    assert(agent->setDisplayColorBuffer && agent->getDisplayColorBuffer && agent->getColorBufferDisplay);
    uint32_t value = 99;
    assert(agent->getDisplayColorBuffer(0, &value) == 0 && value == 0);
    assert(agent->setDisplayColorBuffer(0, 5) < 0);
    auto first = std::make_shared<Object>();
    std::weak_ptr<Object> firstWeak = first;
    registry.add(5, first);
    first.reset();
    assert(agent->setDisplayColorBuffer(0, 5) == 0);
    assert(agent->getDisplayColorBuffer(0, &value) == 0 && value == 5);
    assert(agent->getColorBufferDisplay(5, &value) == 0 && value == 0);
    registry.erase(5);
    assert(firstWeak.expired() && agent->getDisplayColorBuffer(0, &value) < 0);
    auto second = std::make_shared<Object>();
    registry.add(6, second);
    assert(agent->setDisplayColorBuffer(0, 6) == 0);
    assert(firstWeak.expired());
    registry.erase(6);
    auto replacement = std::make_shared<Object>();
    registry.add(6, replacement);
    assert(agent->getDisplayColorBuffer(0, &value) < 0); // Same number, different object.
    assert(agent->setDisplayColorBuffer(0, 6) == 0);
    assert(agent->getDisplayColorBuffer(0, &value) == 0 && value == 6);
    assert(agent->setDisplayColorBuffer(0, 0) == 0);
    assert(agent->getDisplayColorBuffer(0, &value) == 0 && value == 0);
    auto transient = std::make_shared<Object>();
    registry.add(7, transient);
    assert(agent->setDisplayColorBuffer(0, 7) < 0);
    assert(agent->getDisplayColorBuffer(0, &value) == 0 && value == 0);
    auto pending = std::make_shared<Object>();
    std::weak_ptr<Object> pendingWeak = pending;
    registry.add(8, pending);
    assert(agent->setDisplayColorBuffer(0, 8) == 0);
    pending.reset();
    shouldBlock = true;
    std::atomic<int> result{0};
    std::thread inFlight([&] { uint32_t observed = 0; result = agent->getDisplayColorBuffer(0, &observed); });
    {
        std::unique_lock<std::mutex> lock(testMutex);
        assert(testCv.wait_for(lock, std::chrono::seconds(2), [] { return blockedLookup; }));
    }
    registry.erase(8);
    assert(!pendingWeak.expired()); // Weak reference was promoted by the active callback.
    assert(rme::stopRenderer(token, 1) == rme::RendererStopResult::PendingCallbacks);
    assert(!rme::clearPrimaryDisplay(token, 1));
    assert(agent->getDisplayColorBuffer(0, &value) < 0);
    {
        std::lock_guard<std::mutex> lock(testMutex);
        releaseLookup = true;
    }
    testCv.notify_all();
    inFlight.join();
    assert(result < 0);
    assert(pendingWeak.expired()); // Promotion is released before quiesce succeeds.
    assert(rme::stopRenderer(token, 1) == rme::RendererStopResult::NoRenderer);
    assert(agent->getDisplayColorBuffer(0, &value) < 0);
    registry.erase(6);
    std::puts("PRIMARY_DISPLAY_REGISTERED_COLOR_ASSOCIATION_OK");
}
