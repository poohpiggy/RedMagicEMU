#include "renderer_bootstrap.h"
#include "host-common/opengles.h"
#include <cassert>
#include <cstdio>
#include <string>

// All operations are substitutes. No actual renderer, window or socket exists.
static std::string calls;
static rme::RendererStartResult startResult = rme::RendererStartResult::Started;
static rme::RendererStopResult stopResult = rme::RendererStopResult::Stopped;
static int showResult = 0, hideResult = 0;
static bool publishResult = true;
namespace rme {
RendererStartResult startRenderer(const RendererStart&) { calls += 'S'; return startResult; }
RendererStopResult stopRenderer(uint64_t token, uint64_t generation) {
    assert(token == 4 && generation == 8); calls += 'T'; return stopResult;
}
}
int android_showOpenglesWindow(void* window, int x, int y, int w, int h, int fbw, int fbh,
                               float dpr, float rotation, bool replace, bool hide) {
    assert(window == reinterpret_cast<void*>(1));
    assert(x == 0 && y == 0 && w == 320 && h == 480 && fbw == 640 && fbh == 960);
    assert(dpr == 1 && rotation == 0 && !replace && !hide);
    calls += 'W'; return showResult;
}
int android_hideOpenglesWindow() { calls += 'H'; return hideResult; }
static bool publish(void*) { calls += 'P'; return publishResult; }
int main() {
    rme::RendererStart params{};
    params.width = 640; params.height = 960; params.display_token = 4; params.generation = 8;
    auto run = [&] { return rme::bootstrapRenderer(params, reinterpret_cast<void*>(1), 320, 480, publish, nullptr); };
    assert(rme::bootstrapRenderer(params, nullptr, 320, 480, publish, nullptr) == rme::RendererBootstrapResult::InvalidArguments && calls.empty());
    params.headless = true;
    assert(run() == rme::RendererBootstrapResult::InvalidArguments && calls.empty());
    params.headless = false;
    startResult = rme::RendererStartResult::GpuRejected;
    assert(run() == rme::RendererBootstrapResult::RendererRejected && calls == "S");
    calls.clear(); startResult = rme::RendererStartResult::StartFailed;
    assert(run() == rme::RendererBootstrapResult::RendererRejected && calls == "ST");
    calls.clear(); stopResult = rme::RendererStopResult::Failed;
    assert(run() == rme::RendererBootstrapResult::CleanupFailed && calls == "ST");
    stopResult = rme::RendererStopResult::Stopped;
    calls.clear(); startResult = rme::RendererStartResult::Started; showResult = -1;
    assert(run() == rme::RendererBootstrapResult::WindowFailed && calls == "SWT");
    calls.clear(); stopResult = rme::RendererStopResult::Failed;
    assert(run() == rme::RendererBootstrapResult::CleanupFailed && calls == "SWT");
    calls.clear(); showResult = 0; stopResult = rme::RendererStopResult::Stopped; publishResult = false;
    assert(run() == rme::RendererBootstrapResult::PublishFailed && calls == "SWPHT");
    calls.clear(); hideResult = -1;
    assert(run() == rme::RendererBootstrapResult::CleanupFailed && calls == "SWPHT");
    calls.clear(); hideResult = 0; publishResult = true;
    assert(run() == rme::RendererBootstrapResult::Started && calls == "SWP");
    std::puts("RENDERER_WINDOW_BEFORE_GUEST_PUBLICATION_OK");
}
