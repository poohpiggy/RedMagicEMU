#include "renderer_start.h"
#include "primary_display.h"
#include "vm_snapshot.h"
#include "host-common/vm_operations.h"
#include "host-common/window_agent.h"
#include "host-common/multi_display_agent.h"
#include "gfxstream/host/Features.h"
#include <cassert>
#include <cstdio>

int main() {
    rme::RendererStart params{};
    assert(rme::startRenderer(params) == rme::RendererStartResult::InvalidArguments);
    // Deliberately leave the real gfxstream host selection unconfigured. None
    // of these data-only arguments is called and no renderer/GPU is started.
    uint64_t vmToken = rme::bindSnapshotVm(1, rme::SnapshotCapability::Unavailable);
    assert(vmToken);
    const auto* vm = rme::snapshotVmAgent();
    QAndroidEmulatorWindowAgent window{};
    gfxstream::host::FeatureSet features{};
    params = {640, 480, true, 36, vm, &window, rme::primaryDisplayAgent(), &features};
    assert(rme::startRenderer(params) == rme::RendererStartResult::MissingAgentFunctions);
    assert(rme::clearSnapshotVm(vmToken, 1));
    std::puts("RENDERER_START_REJECTS_INCOMPLETE_AGENTS");
}
