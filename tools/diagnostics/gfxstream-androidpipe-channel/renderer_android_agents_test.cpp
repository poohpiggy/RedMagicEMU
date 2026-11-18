#include "renderer_start.h"
#include "primary_display.h"
#include "vm_snapshot.h"
#include "host-common/window_agent.h"
#include "host-common/multi_display_agent.h"
#include "gfxstream/host/Features.h"
#include "../gfxstream-android-gpuinfo/query.h"
#include <cassert>
#include <cstdio>

namespace rme {
// This callback-set test never reaches GPU enumeration.
GpuInfoResult nativeGpuInfoResultFor(const GpuInfoList*) { return {GpuInfoStatus::NotQueried, VK_SUCCESS}; }
}

// These functions make the test's other agent entries non-null; none is called.
static int create(uint32_t*) { return -1; }
static int destroy(uint32_t) { return -1; }
static int poseSet(uint32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t) { return -1; }
static int poseGet(uint32_t, int32_t*, int32_t*, uint32_t*, uint32_t*) { return -1; }
static int colorGet(uint32_t, uint32_t*) { return -1; }
static int colorSet(uint32_t, uint32_t) { return -1; }
int main() {
    auto displayToken = rme::bindPrimaryDisplay(1, 640, 480, 160, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    auto vmToken = rme::bindSnapshotVm(1, rme::SnapshotCapability::Unavailable);
    assert(displayToken && vmToken);
    auto display = *rme::primaryDisplayAgent();
    display.createDisplay = create;
    display.destroyDisplay = destroy;
    display.setDisplayPose = poseSet;
    display.getDisplayPose = poseGet;
    display.getDisplayColorBuffer = colorGet;
    display.getColorBufferDisplay = colorGet;
    display.setDisplayColorBuffer = colorSet;
    QAndroidEmulatorWindowAgent window = *rme::primaryWindowAgent();
    assert(!window.runOnUiThread && !window.isRunningInUiThread);
    gfxstream::host::FeatureSet features{};
    rme::RendererStart params{640, 480, true, 36, rme::snapshotVmAgent(), &window, &display, &features};
    // A valid Android callback set reaches the host-configuration check. The
    // test never selects host or initializes a renderer / Vulkan loader.
    assert(rme::startRenderer(params) == rme::RendererStartResult::HostNotSelected);
    assert(rme::clearPrimaryDisplay(displayToken, 1));
    assert(rme::clearSnapshotVm(vmToken, 1));
    std::puts("ANDROID_RENDERER_UI_CALLBACKS_NOT_REQUIRED_OK");
}
