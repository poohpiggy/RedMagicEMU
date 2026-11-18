#pragma once
#include "renderer_start.h"

namespace rme {
enum class RendererBootstrapResult {
    InvalidArguments, RendererRejected, WindowFailed, PublishFailed, CleanupFailed, Started
};
// Caller runs this on the renderer/device thread. `nativeWindow` is a live,
// separately owned ANativeWindow from this process, not the GLES window. The
// publication callback must be atomic: on false it must leave no client able
// to connect; on true it takes responsibility for closing the service before
// stopping the renderer. No service or Guest process may access the renderer
// before this call invokes the publication callback.
RendererBootstrapResult bootstrapRenderer(const RendererStart&, void* nativeWindow,
                                           int windowWidth, int windowHeight,
                                           bool (*publish)(void*), void* publishContext);
}
