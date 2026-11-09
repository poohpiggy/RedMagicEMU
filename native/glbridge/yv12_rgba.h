#ifndef AEMU_YV12_RGBA_H
#define AEMU_YV12_RGBA_H

#include <stddef.h>
#include <stdint.h>

/* Each plane is a readable byte range starting at data. The caller must obtain
 * its true available size from a validated backing allocation. These sizes are
 * not supplied by AHardwareBuffer_lockPlanes. Only 8-bit planar YV12 is handled:
 * pixel stride 1, Y/U/V passed in semantic order (the storage may be Y/V/U).
 * The result is RGBA8888, full alpha, using BT.601 limited-range coefficients.
 * Other color spaces, transfers, HDR, and protected buffers are not handled. */
struct aemu_yv12_plane {
    const uint8_t *data;
    size_t size;
    size_t stride;
};

static inline int aemu_yv12_span(size_t stride, size_t rows, size_t cols,
                                  size_t available, size_t *span) {
    if (!rows || !cols || stride < cols || (rows - 1) > (SIZE_MAX - cols) / stride) return 0;
    *span = (rows - 1) * stride + cols;
    return *span <= available;
}

static inline int aemu_yv12_address_range(const void *ptr, size_t span,
                                           uintptr_t *start, uintptr_t *end) {
    if (!ptr) return 0;
    *start = (uintptr_t)ptr;
    if (span > UINTPTR_MAX - *start) return 0;
    *end = *start + span;
    return 1;
}

/* Value is scaled by 256 and already has the rounding term included. */
static inline uint8_t aemu_yv12_clamp(int value) {
    if (value <= 0) return 0;
    if (value >= 255 * 256) return 255;
    return (uint8_t)(value / 256);
}

/* Returns 1 on success; on validation failure returns 0 without writing dst. */
static inline int aemu_yv12_to_rgba(uint8_t *dst, size_t dst_size, size_t dst_stride,
                                     size_t width, size_t height,
                                     struct aemu_yv12_plane y,
                                     struct aemu_yv12_plane u,
                                     struct aemu_yv12_plane v) {
    if (!width || !height || width > 8192 || height > 8192) return 0;
    size_t cw = (width + 1) / 2, ch = (height + 1) / 2;
    size_t dspan, yspan, uspan, vspan;
    if (!aemu_yv12_span(dst_stride, height, width * 4, dst_size, &dspan) ||
        !aemu_yv12_span(y.stride, height, width, y.size, &yspan) ||
        !aemu_yv12_span(u.stride, ch, cw, u.size, &uspan) ||
        !aemu_yv12_span(v.stride, ch, cw, v.size, &vspan)) return 0;
    uintptr_t ds, de, ys, ye, us, ue, vs, ve;
    if (!aemu_yv12_address_range(dst, dspan, &ds, &de) ||
        !aemu_yv12_address_range(y.data, yspan, &ys, &ye) ||
        !aemu_yv12_address_range(u.data, uspan, &us, &ue) ||
        !aemu_yv12_address_range(v.data, vspan, &vs, &ve) ||
        (ds < ye && ys < de) || (ds < ue && us < de) || (ds < ve && vs < de)) return 0;

    for (size_t row = 0; row < height; row++) {
        uint8_t *d = dst + row * dst_stride;
        const uint8_t *py = y.data + row * y.stride;
        const uint8_t *pu = u.data + (row / 2) * u.stride;
        const uint8_t *pv = v.data + (row / 2) * v.stride;
        for (size_t col = 0; col < width; col++) {
            int c = (int)py[col] - 16;
            int cb = (int)pu[col / 2] - 128;
            int cr = (int)pv[col / 2] - 128;
            if (c < 0) c = 0;
            d[col * 4] = aemu_yv12_clamp(298 * c + 409 * cr + 128);
            d[col * 4 + 1] = aemu_yv12_clamp(298 * c - 100 * cb - 208 * cr + 128);
            d[col * 4 + 2] = aemu_yv12_clamp(298 * c + 516 * cb + 128);
            d[col * 4 + 3] = 255;
        }
    }
    return 1;
}
#endif
