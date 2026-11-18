#include "renderer_bootstrap.h"
#include "host-common/opengles.h"

namespace rme {
namespace {
bool cleanup(uint64_t token, uint64_t generation) {
    const auto result = stopRenderer(token, generation);
    return result == RendererStopResult::Stopped || result == RendererStopResult::NoRenderer;
}
}
RendererBootstrapResult bootstrapRenderer(const RendererStart& params, void* nativeWindow,
                                           int width, int height,
                                           bool (*publish)(void*), void* context) {
    if (!nativeWindow || width <= 0 || height <= 0 || !publish || params.headless)
        return RendererBootstrapResult::InvalidArguments;
    const auto started = startRenderer(params);
    if (started != RendererStartResult::Started) {
        if (started == RendererStartResult::StartFailed && !cleanup(params.display_token, params.generation))
            return RendererBootstrapResult::CleanupFailed;
        return RendererBootstrapResult::RendererRejected;
    }
    if (android_showOpenglesWindow(nativeWindow, 0, 0, width, height, params.width, params.height,
                                   1.0f, 0.0f, false, false) != 0) {
        return cleanup(params.display_token, params.generation) ? RendererBootstrapResult::WindowFailed
                                                                 : RendererBootstrapResult::CleanupFailed;
    }
    if (!publish(context)) {
        const bool hidden = android_hideOpenglesWindow() == 0;
        const bool stopped = cleanup(params.display_token, params.generation);
        return hidden && stopped ? RendererBootstrapResult::PublishFailed : RendererBootstrapResult::CleanupFailed;
    }
    return RendererBootstrapResult::Started;
}
}
