#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../yv12_rgba.h"

static int failures;
static void check(int pass, const char *name) {
    if (!pass) { fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
static int convert(uint8_t *out, size_t out_size, size_t out_stride, size_t w, size_t h,
                   struct aemu_yv12_plane y, struct aemu_yv12_plane u, struct aemu_yv12_plane v) {
    return aemu_yv12_to_rgba(out, out_size, out_stride, w, h, y, u, v);
}
static void rejected_unchanged(const char *name, uint8_t *out, size_t size, size_t stride,
                               size_t w, size_t h, struct aemu_yv12_plane y,
                               struct aemu_yv12_plane u, struct aemu_yv12_plane v) {
    uint8_t before[128];
    memset(out, 0xa5, sizeof(before));
    memcpy(before, out, sizeof(before));
    int ok = convert(out, size, stride, w, h, y, u, v);
    check(!ok && !memcmp(out, before, sizeof(before)), name);
}
int main(void) {
    /* Two rows, padded strides; one red and one blue 2x2 chroma block. */
    uint8_t ydata[12] = {81, 81, 41, 41, 9, 9, 81, 81, 41, 41, 9, 9};
    uint8_t udata[4] = {90, 240, 9, 9};
    uint8_t vdata[4] = {240, 110, 9, 9};
    struct aemu_yv12_plane y = {ydata, sizeof ydata, 6};
    struct aemu_yv12_plane u = {udata, sizeof udata, 4};
    struct aemu_yv12_plane v = {vdata, sizeof vdata, 4};
    uint8_t out[128];
    memset(out, 0xa5, sizeof out);
    check(convert(out, sizeof out, 20, 4, 2, y, u, v), "padded YV12 converts");
    const uint8_t expected[16] = {255,0,0,255, 255,0,0,255, 0,0,255,255, 0,0,255,255};
    check(!memcmp(out, expected, 16) && !memcmp(out + 20, expected, 16), "BT.601 red/blue, plane order, rows");
    check(out[16] == 0xa5 && out[19] == 0xa5 && out[36] == 0xa5, "destination row padding preserved");

    uint8_t yodd[15] = {16,235,16,9,9, 235,16,235,9,9, 16,235,16,9,9};
    uint8_t neutral[6] = {128,128,9,128,128,9};
    struct aemu_yv12_plane oddy = {yodd, sizeof yodd, 5};
    struct aemu_yv12_plane oddc = {neutral, sizeof neutral, 3};
    memset(out, 0xa5, sizeof out);
    check(convert(out, sizeof out, 16, 3, 3, oddy, oddc, oddc), "odd dimensions convert");
    check(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 255 &&
          out[4] == 255 && out[5] == 255 && out[6] == 255 && out[7] == 255 &&
          out[32] == 0 && out[36] == 255, "BT.601 black/white and odd chroma sampling");

    rejected_unchanged("reject zero width", out, sizeof out, 20, 0, 2, y, u, v);
    rejected_unchanged("reject zero height", out, sizeof out, 20, 4, 0, y, u, v);
    rejected_unchanged("reject excessive dimensions", out, sizeof out, 20, 8193, 2, y, u, v);
    rejected_unchanged("reject short output", out, 35, 20, 4, 2, y, u, v);
    rejected_unchanged("reject short output stride", out, sizeof out, 15, 4, 2, y, u, v);
    rejected_unchanged("reject output stride overflow", out, SIZE_MAX, SIZE_MAX, 4, 2, y, u, v);
    struct aemu_yv12_plane shorty = {ydata, 9, 6};
    struct aemu_yv12_plane shortu = {udata, 1, 4};
    struct aemu_yv12_plane shortv = {vdata, 1, 4};
    struct aemu_yv12_plane bigstride = {ydata, SIZE_MAX, SIZE_MAX};
    struct aemu_yv12_plane shortstride = {ydata, sizeof ydata, 3};
    struct aemu_yv12_plane nullplane = {NULL, sizeof ydata, 6};
    struct aemu_yv12_plane badaddr = {(const uint8_t *)(uintptr_t)(UINTPTR_MAX - 1), SIZE_MAX, 6};
    rejected_unchanged("reject short Y", out, sizeof out, 20, 4, 2, shorty, u, v);
    rejected_unchanged("reject short U", out, sizeof out, 20, 4, 2, y, shortu, v);
    rejected_unchanged("reject short V", out, sizeof out, 20, 4, 2, y, u, shortv);
    rejected_unchanged("reject Y stride overflow", out, sizeof out, 20, 4, 2, bigstride, u, v);
    rejected_unchanged("reject short Y stride", out, sizeof out, 20, 4, 2, shortstride, u, v);
    rejected_unchanged("reject null plane", out, sizeof out, 20, 4, 2, nullplane, u, v);
    rejected_unchanged("reject plane address overflow", out, sizeof out, 20, 4, 2, badaddr, u, v);
    struct aemu_yv12_plane alias = {out, 20, 6};
    rejected_unchanged("reject output overlapping input", out, sizeof out, 20, 4, 2, alias, u, v);
    if (failures) { fprintf(stderr, "%d checks failed\n", failures); return 1; }
    puts("YV12_RGBA_OK");
    return 0;
}
