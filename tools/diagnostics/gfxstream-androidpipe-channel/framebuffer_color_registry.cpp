#include "host_color_registry.h"
#include "FrameBuffer.h"

namespace rme {
std::shared_ptr<void> findRegisteredColorBuffer(uint32_t handle) {
    auto* fb = gfxstream::FrameBuffer::getFB();
    return fb && handle ? fb->findColorBuffer(handle) : nullptr;
}
}
