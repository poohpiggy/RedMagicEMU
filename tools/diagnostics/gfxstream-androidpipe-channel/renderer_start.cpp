#include "renderer_start.h"
#include "primary_display.h"
#include "host-common/opengles.h"
#include "host-common/opengl/emugl_config.h"
#include "host-common/opengl/gpuinfo.h"
#include "render-utils/render_api.h"
#if defined(ANDROID)
#include "../gfxstream-android-gpuinfo/query.h"
extern "C" int rme_set_renderer_headless();
#endif

namespace rme {
namespace { bool ownedRenderer = false; }
RendererStartResult startRenderer(const RendererStart& p) {
    if (p.width <= 0 || p.height <= 0 || p.guest_api <= 0 || !p.vm || !p.window || !p.display || !p.features)
        return RendererStartResult::InvalidArguments;
    // Fixed Android gfxstream posts on its own worker; the UI callbacks are
    // only used on the upstream Apple main-thread posting path.
#if !defined(ANDROID)
    if (!p.window->runOnUiThread || !p.window->isRunningInUiThread)
        return RendererStartResult::MissingAgentFunctions;
#endif
    if (!p.vm->setSkipSnapshotSave || !p.window->isFolded || !p.window->getFoldedArea || !p.display->getMultiDisplay ||
        !p.display->isMultiDisplayEnabled || !p.display->isPixelFold ||
        !p.display->getNextMultiDisplay || !p.display->getDisplayColorBuffer ||
        !p.display->getColorBufferDisplay || !p.display->createDisplay || !p.display->destroyDisplay ||
        !p.display->setDisplayColorBuffer || !p.display->getDisplayPose || !p.display->setDisplayPose)
        return RendererStartResult::MissingAgentFunctions;
    if (emuglConfig_get_current_renderer() != SELECTED_RENDERER_HOST)
        return RendererStartResult::HostNotSelected;
    if (!primaryColorBuffersBound(p.display_token, p.generation) || p.display != primaryDisplayAgent())
        return RendererStartResult::InvalidBinding;
#if defined(ANDROID)
    // Wait for the same upstream worker used by the blacklist checks. An
    // empty/failed or unrelated query must never become a blacklist miss.
    const auto& gpuInfo = globalGpuInfoList();
    if (nativeGpuInfoResultFor(&gpuInfo).status != GpuInfoStatus::Ok)
        return RendererStartResult::GpuRejected;
#endif
    if (isHostGpuBlacklisted() || async_query_host_gpu_VulkanBlacklisted())
        return RendererStartResult::GpuRejected;
    static bool attempted = false;
    if (attempted || android_getOpenglesRenderer()) return RendererStartResult::AlreadyAttempted;
    attempted = true;
    // Upstream holds a raw pointer and process-global callbacks. Keep the
    // library alive for the remainder of the process even after startup failure.
    static auto* library = new gfxstream::RenderLibPtr(gfxstream::initLibrary());
    if (!*library) return RendererStartResult::LibraryFailed;
    if (android_prepareOpenglesEmulation() != 0) return RendererStartResult::PrepareFailed;
    if (p.headless) {
#if defined(ANDROID)
        if (rme_set_renderer_headless() != 0) return RendererStartResult::SetupFailed;
#else
        return RendererStartResult::SetupFailed;
#endif
    }
    if (android_setOpenglesEmulation(library->get(), nullptr, nullptr) != 0)
        return RendererStartResult::SetupFailed;
    int major = 0, minor = 0;
    int result = android_startOpenglesRenderer(p.width, p.height, p.phone, p.guest_api, p.vm, p.window,
                                               p.display, p.features, &major, &minor);
    ownedRenderer = static_cast<bool>(android_getOpenglesRenderer());
    if (result != 0 || !ownedRenderer) return RendererStartResult::StartFailed;
    return RendererStartResult::Started;
}
RendererStopResult stopRenderer(uint64_t token, uint64_t generation) {
    if (!primaryColorBuffersBound(token, generation)) return RendererStopResult::InvalidBinding;
    const bool hasRenderer = static_cast<bool>(android_getOpenglesRenderer());
    if (hasRenderer && !ownedRenderer) return RendererStopResult::ForeignRenderer;
    if (!quiescePrimaryColorBuffers(token, generation)) return RendererStopResult::PendingCallbacks;
    // This upstream entry does nothing if no renderer was ever started.
    android_stopOpenglesRenderer(true);
    if (android_getOpenglesRenderer()) return RendererStopResult::Failed;
    ownedRenderer = false;
    if (!clearPrimaryDisplay(token, generation)) return RendererStopResult::Failed;
    return hasRenderer ? RendererStopResult::Stopped : RendererStopResult::NoRenderer;
}
}
