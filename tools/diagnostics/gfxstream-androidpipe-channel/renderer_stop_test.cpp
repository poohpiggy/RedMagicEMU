#include "renderer_start.h"
#include "primary_display.h"
#include "host-common/opengles.h"
#include "gfxstream/host/Features.h"
#include <cassert>
#include <cstdio>

int main() {
    assert(!android_getOpenglesRenderer());
    QAndroidVmOperations vm{};
    QAndroidEmulatorWindowAgent window{};
    QAndroidMultiDisplayAgent display{};
    gfxstream::host::FeatureSet features{};
    int major = 0, minor = 0;
    // Fresh process: no RenderLib has been installed. The real upstream entry
    // must reject before touching the renderer, agents or graphics driver.
    assert(android_startOpenglesRenderer(640, 480, true, 36, &vm, &window, &display,
                                         &features, &major, &minor) == -1);
    assert(!android_getOpenglesRenderer());
    assert(rme::stopRenderer(0, 0) == rme::RendererStopResult::InvalidBinding);
    auto token = rme::bindPrimaryDisplay(1, 640, 480, 160,
                                         rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    assert(token && rme::enablePrimaryColorBuffers(token, 1));
    // The actual upstream stop is safe with no renderer and must not be
    // reported as a successful GPU shutdown.
    assert(rme::stopRenderer(token, 1) == rme::RendererStopResult::NoRenderer);
    assert(!android_getOpenglesRenderer());
    assert(!rme::primaryWindowAgent());
    std::puts("REAL_RENDERER_ABSENT_STOP_CLEANS_BINDING");
}
