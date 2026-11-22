#pragma once

#include <C2Buffer.h>
#include <cstring>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

enum class CameraChromaCopy { Nv12Rows, Nv21Swap, Repacked };

// Copy to packed NV12 and report which operation was used.
// The caller validates the plane pointers, byte depths, and configured dimensions.
static inline CameraChromaCopy copyCameraChroma(uint8_t *out, const uint8_t *pu, const uint8_t *pv,
                                               const C2PlanarLayout &layout, const C2Rect &crop,
                                               uint32_t width, uint32_t height) {
    const auto &y = layout.planes[C2PlanarLayout::PLANE_Y];
    const auto &u = layout.planes[C2PlanarLayout::PLANE_U];
    const auto &v = layout.planes[C2PlanarLayout::PLANE_V];
    // The mapped plane pointers do not track the view's crop origin.
    const bool packed = crop.left == 0 && crop.top == 0 && crop.width >= width && crop.height >= height &&
                        layout.rootPlanes == 2 && y.rootIx == C2PlanarLayout::PLANE_Y && y.offset == 0 &&
                        y.channel == C2PlaneInfo::CHANNEL_Y && u.channel == C2PlaneInfo::CHANNEL_CB &&
                        v.channel == C2PlaneInfo::CHANNEL_CR &&
                        y.colSampling == 1 && y.rowSampling == 1 &&
                        u.colSampling == 2 && u.rowSampling == 2 && v.colSampling == 2 && v.rowSampling == 2 &&
                        y.bitDepth == 8 && u.bitDepth == 8 && v.bitDepth == 8 &&
                        y.rightShift == 0 && u.rightShift == 0 && v.rightShift == 0 &&
                        u.colInc == 2 && v.colInc == 2 && u.rowInc == v.rowInc &&
                        u.rowInc >= static_cast<int32_t>(width);
    if (packed && u.rootIx == C2PlanarLayout::PLANE_U && v.rootIx == C2PlanarLayout::PLANE_U &&
        u.offset == 0 && v.offset == 1 && pv == pu + 1) {
        for (uint32_t row = 0; row < height / 2; ++row) {
            std::memcpy(out, pu + static_cast<size_t>(row) * u.rowInc, width);
            out += width;
        }
        return CameraChromaCopy::Nv12Rows;
    }
    if (packed && u.rootIx == C2PlanarLayout::PLANE_V && v.rootIx == C2PlanarLayout::PLANE_V &&
        u.offset == 1 && v.offset == 0 && pu == pv + 1) {
        for (uint32_t row = 0; row < height / 2; ++row) {
            const uint8_t *vu = pv + static_cast<size_t>(row) * v.rowInc;
            uint32_t x = 0;
#if defined(__ARM_NEON)
            for (; x + 16 <= width; x += 16)
                vst1q_u8(out + x, vrev16q_u8(vld1q_u8(vu + x)));
#endif
            for (; x < width; x += 2) {
                out[x] = vu[x + 1];
                out[x + 1] = vu[x];
            }
            out += width;
        }
        return CameraChromaCopy::Nv21Swap;
    }

    for (uint32_t row = 0; row < height / 2; ++row) {
        const uint8_t *su = pu + static_cast<size_t>(row) * u.rowInc;
        const uint8_t *sv = pv + static_cast<size_t>(row) * v.rowInc;
        uint32_t x = 0;
#if defined(__ARM_NEON)
        if (u.colInc == 1 && v.colInc == 1) {
            for (; x + 16 <= width / 2; x += 16) {
                const uint8x16x2_t uv = {{vld1q_u8(su + x), vld1q_u8(sv + x)}};
                vst2q_u8(out + 2 * x, uv);
            }
        }
#endif
        for (; x < width / 2; ++x) {
            out[2 * x] = su[x * u.colInc];
            out[2 * x + 1] = sv[x * v.colInc];
        }
        out += width;
    }
    return CameraChromaCopy::Repacked;
}
