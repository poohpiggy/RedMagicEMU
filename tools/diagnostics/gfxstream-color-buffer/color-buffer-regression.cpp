// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstdio>

#include "color_buffer.h"

using namespace gfxstream::host;

int main() {
    auto cb = ColorBuffer::create(nullptr, nullptr, 4, 4,
                                  GfxstreamFormat::R8G8B8A8_UNORM, 123, nullptr, false);
    if (!cb) {
        std::puts("FAIL cannot create no-backing ColorBuffer");
        return 2;
    }
    if (cb->getColorBufferGl() || cb->getColorBufferVk()) {
        std::puts("FAIL unexpected backing");
        return 2;
    }

    uint8_t pixels[64] = {};
    bool syncs = cb->flushFromGl() && cb->flushFromVk() &&
                 cb->flushFromVkBytes(pixels, sizeof pixels) &&
                 cb->invalidateForBackend(Backend::GL) &&
                 cb->invalidateForBackend(Backend::VK);
    std::printf("no_backing_syncs=%d\n", syncs);
    if (!syncs) {
        std::puts("FAIL existing no-sync-needed contract");
        return 1;
    }

    bool updated = cb->updateGlFromBytes(pixels, sizeof pixels);
    std::printf("gl_update_without_backing=%d\n", updated);
    if (updated) {
        std::puts("FAIL GL update incorrectly reported success without GL backing");
        return 1;
    }
    return 0;
}
