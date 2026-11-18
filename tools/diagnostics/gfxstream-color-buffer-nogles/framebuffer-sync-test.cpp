// SPDX-License-Identifier: MIT
// Isolated FrameBuffer initialization, real Vulkan color buffer, round-trip, cleanup.
#include <array>
#include <cstdint>
#include <cstdio>
#include "frame_buffer.h"
#include "gfxstream/host/features.h"
using namespace gfxstream::host;
int main() {
    FeatureSet features;
    features.Vulkan.setEnabled(true);
    features.GuestVulkanOnly.setEnabled(true);
    features.GlDirectMem.setEnabled(true);
    features.SystemBlob.setEnabled(true);
    if (!features.processFeatureString("VulkanExternalMemoryMode:HostAllocation", "research")) return 2;
    if (!FrameBuffer::initialize(4, 4, features, false)) {
        std::puts("FAIL framebuffer initialization");
        return 1;
    }
    int result = 0;
    bool created = false;
    constexpr HandleType handle = 4123;
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb || !fb->hasEmulationVk() || fb->hasEmulationGl()) {
        std::puts("FAIL expected real Vulkan and no GLES"); result = 1;
    } else if (!fb->createColorBufferWithResourceHandle(4, 4, GfxstreamFormat::R8G8B8A8_UNORM, handle)) {
        std::puts("FAIL create Vulkan color buffer"); result = 1;
    } else {
        created = true;
        std::array<uint8_t, 64> expected, actual;
        for (size_t i=0; i<expected.size(); ++i) expected[i]=static_cast<uint8_t>(i*3+7);
        actual.fill(0);
        if (!fb->updateColorBuffer(handle, 0, 0, 4, 4, GfxstreamFormat::R8G8B8A8_UNORM, expected.data())) {
            std::puts("FAIL update Vulkan color buffer"); result = 1;
        } else {
            fb->readColorBuffer(handle, 0, 0, 4, 4, GfxstreamFormat::R8G8B8A8_UNORM, actual.data(), actual.size());
            if (actual != expected) { std::puts("FAIL pixel roundtrip mismatch"); result = 1; }
            else {
                std::puts("PIXEL_ROUNDTRIP_OK 64");
                std::array<uint8_t, 64> other; other.fill(0xA5);
                if (!fb->invalidateColorBufferForVk(handle) || !fb->flushColorBufferFromVk(handle) ||
                    !fb->flushColorBufferFromVkBytes(handle, other.data(), other.size())) {
                    std::puts("FAIL valid Vulkan sync returned false"); result = 1;
                }
                actual.fill(0);
                fb->readColorBuffer(handle, 0, 0, 4, 4, GfxstreamFormat::R8G8B8A8_UNORM, actual.data(), actual.size());
                if (actual != expected) { std::puts("FAIL Vulkan content changed during GL sync"); result = 1; }
                else std::puts("VK_ONLY_SYNC_PRESERVES_PIXELS 64");
                if (fb->invalidateColorBufferForVk(handle+1) || fb->flushColorBufferFromVk(handle+1) ||
                    fb->flushColorBufferFromVkBytes(handle+1, other.data(), other.size())) {
                    std::puts("FAIL unknown handle reported sync success"); result = 1;
                } else std::puts("UNKNOWN_HANDLE_SYNC_REJECTED");
            }
        }
    }
    if (created) { fb->closeColorBuffer(handle); std::puts("COLOR_BUFFER_CLOSE_RETURNED"); }
    FrameBuffer::finalize();
    std::puts("FRAMEBUFFER_FINALIZE_RETURNED");
    return result;
}
