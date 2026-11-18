#pragma once
#include <cstdint>
struct QAndroidVmOperations;
struct QAndroidEmulatorWindowAgent;
struct QAndroidMultiDisplayAgent;
namespace gfxstream { namespace host { struct FeatureSet; } }
namespace rme {
struct RendererStart {
    int width;
    int height;
    bool phone;
    int guest_api;
    const QAndroidVmOperations* vm;
    const QAndroidEmulatorWindowAgent* window;
    const QAndroidMultiDisplayAgent* display;
    const gfxstream::host::FeatureSet* features;
    uint64_t display_token = 0;
    uint64_t generation = 0;
    bool headless = false;
};
enum class RendererStartResult {
    InvalidArguments, HostNotSelected, MissingAgentFunctions, InvalidBinding, GpuRejected, AlreadyAttempted, LibraryFailed,
    PrepareFailed, SetupFailed, StartFailed, Started
};
// Renderer and agents are process-global upstream. Call once on the device
// thread after the caller configures real agents and selects the host renderer.
// Agents must remain valid until all renderer work is stopped. Does not bind a
// window or expose an endpoint.
RendererStartResult startRenderer(const RendererStart&);
enum class RendererStopResult { InvalidBinding, ForeignRenderer, PendingCallbacks, Failed, NoRenderer, Stopped };
// Call on the renderer/device thread; retry PendingCallbacks before any upstream
// FrameBuffer teardown. Caller must stop new client traffic first.
RendererStopResult stopRenderer(uint64_t display_token, uint64_t generation);
}
