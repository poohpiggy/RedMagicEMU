/*
 * copybit.default.so - copybit HAL of AEmulator for Android 1.x (Donut hardware/copybit.h).
 *
 * Why: Donut's SurfaceFlinger shows YUV buffers (video: LayerBuffer::BufferSource, e.g. Qualcomm's libopencorehw)
 * through copybit; without one it falls back to OpenGL, where LayerBase::loadTexture uploads only the Y plane of
 * YUV buffers as GL_LUMINANCE, so video plays in grey. Firmwares whose vendor copybit drives an MDP we don't have
 * ship none for their board (copybit.<board>.so missing), so this one is loaded as copybit.default.so.
 *
 * What: stretch() of YCbCr 4:2:0 / 4:2:2 semi-planar sources into RGB 565 / RGBA / BGRA / RGBX destinations, with
 * the transform (flips, 90 degree steps), plane alpha and clip region SurfaceFlinger passes; blit() of a picture into
 * one of the same format (EGLDisplaySurface::copyFrontToBack). Anything else returns -EINVAL and SurfaceFlinger draws
 * it with OpenGL as before (Layer::onDraw and BufferSource::onDraw: "if (!can_use_copybit || err)").
 * Scaling limits are reported large, so BufferSource never takes its two-pass path through a temporary bitmap.
 *
 * Only libc is used.
 */
#include <stdint.h>
#include <stddef.h>

extern void *calloc(unsigned long n, unsigned long size);
extern void free(void *p);
extern void *memcpy(void *d, const void *s, unsigned long n);
#define EXPORT __attribute__((visibility("default")))
#define HARDWARE_MODULE_TAG 0x484d4f44   /* HMOD */
#define HARDWARE_DEVICE_TAG 0x48574454   /* HWDT */
#define EINVAL 22

/* hardware.h (Donut) */
enum { FMT_RGBA_8888 = 1, FMT_RGBX_8888 = 2, FMT_RGB_565 = 4, FMT_BGRA_8888 = 5, FMT_YCbCr_422_SP = 0x10,
       FMT_YCbCr_420_SP = 0x11 };
enum { T_FLIP_H = 1, T_FLIP_V = 2, T_ROT_90 = 4 };
/* copybit.h (Donut) */
enum { P_ROTATION_DEG = 1, P_PLANE_ALPHA = 2, P_DITHER = 3, P_TRANSFORM = 4, P_BLUR = 5 };
enum { G_MINIFICATION_LIMIT = 1, G_MAGNIFICATION_LIMIT = 2, G_SCALING_FRAC_BITS = 3, G_ROTATION_STEP_DEG = 4 };

struct hw_module;
struct hw_device;
struct hw_module_methods { int (*open)(const struct hw_module *m, const char *id, struct hw_device **dev); };
struct hw_module {
    uint32_t tag;
    uint16_t version_major, version_minor;
    const char *id, *name, *author;
    struct hw_module_methods *methods;
    uint32_t reserved[32 - 7];
};
struct hw_device {
    uint32_t tag, version;
    struct hw_module *module;
    uint32_t reserved[12];
    int (*close)(struct hw_device *dev);
};
struct image { uint32_t w, h; int32_t format; uint32_t offset; void *base; int fd; };
struct rect { int l, t, r, b; };
struct region { int (*next)(struct region const *region, struct rect *rect); };
struct device {
    struct hw_device common;
    int (*set_parameter)(struct device *dev, int name, int value);
    int (*get)(struct device *dev, int name);
    int (*blit)(struct device *dev, struct image const *dst, struct image const *src, struct region const *region);
    int (*stretch)(struct device *dev, struct image const *dst, struct image const *src, struct rect const *dst_rect,
                   struct rect const *src_rect, struct region const *region);
    /* ours */
    int transform, alpha;
};

static int bpp(int f) {
    switch (f) {
    case FMT_RGB_565: return 2;
    case FMT_RGBA_8888: case FMT_RGBX_8888: case FMT_BGRA_8888: return 4;
    }
    return 0;
}
static int is_yuv(int f) { return f == FMT_YCbCr_420_SP || f == FMT_YCbCr_422_SP; }
static int clampu8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
/* Android 1.x libc has no __aeabi_uidiv (ARMv5/v7-A without a divide instruction) */
static uint32_t udiv(uint32_t n, uint32_t d) {
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) { r = (r << 1) | ((n >> i) & 1); if (r >= d) { r -= d; q |= 1u << i; } }
    return q;
}
/* a * x + (255 - a) * y, divided by 255 (exact for 0..65025) */
static int mix(int x, int y, int a) { int v = x * a + y * (255 - a); return (v + 1 + (v >> 8)) >> 8; }

static int set_parameter(struct device *d, int name, int value) {
    switch (name) {
    case P_ROTATION_DEG:
        d->transform = value == 90 ? T_ROT_90 : value == 180 ? (T_FLIP_H | T_FLIP_V) :
                       value == 270 ? (T_ROT_90 | T_FLIP_H | T_FLIP_V) : 0;
        return 0;
    case P_PLANE_ALPHA: d->alpha = value < 0 ? 0 : value > 255 ? 255 : value; return 0;
    case P_TRANSFORM: d->transform = value & 7; return 0;
    case P_DITHER: case P_BLUR: return 0;
    }
    return -EINVAL;
}
static int get(struct device *d, int name) {
    (void)d;
    switch (name) {
    case G_MINIFICATION_LIMIT: case G_MAGNIFICATION_LIMIT: return 64;   /* any scale in one pass */
    case G_SCALING_FRAC_BITS: return 16;
    case G_ROTATION_STEP_DEG: return 90;
    }
    return -EINVAL;
}

static int blit(struct device *d, struct image const *dst, struct image const *src, struct region const *region) {
    (void)d;
    int b = bpp(src->format);
    if (!b || src->format != dst->format || !region) return -EINVAL;
    const uint8_t *s = (const uint8_t *)src->base + src->offset;
    uint8_t *o = (uint8_t *)dst->base + dst->offset;
    struct rect r;
    while (region->next(region, &r)) {
        if (r.l < 0) r.l = 0;
        if (r.t < 0) r.t = 0;
        if (r.r > (int)src->w) r.r = (int)src->w;
        if (r.r > (int)dst->w) r.r = (int)dst->w;
        if (r.b > (int)src->h) r.b = (int)src->h;
        if (r.b > (int)dst->h) r.b = (int)dst->h;
        for (int y = r.t; y < r.b; y++)
            if (r.r > r.l)
                memcpy(o + ((size_t)y * dst->w + r.l) * b, s + ((size_t)y * src->w + r.l) * b, (size_t)(r.r - r.l) * b);
    }
    return 0;
}

/* YCbCr semi-planar: luma plane w x h, then the chroma pairs at half width (and half height for 4:2:0); BT.601.
 * Byte order Cr, Cb: the order these formats have on the devices of that time — AOSP msm7k copybit maps
 * HAL YCbCr_420_SP / 422_SP to MDP_Y_CBCR_H2V2 / H2V1, "pseudo planar w/ Cb is in MSB" (msm_mdp.h), and Qualcomm's
 * video output (libopencorehw) fills its buffers that way (checked: Cb first plays a brown scene blue). */
static int stretch(struct device *d, struct image const *dst, struct image const *src, struct rect const *dr,
                   struct rect const *sr, struct region const *region) {
    int ob = bpp(dst->format);
    if (!is_yuv(src->format) || !ob || !dr || !sr || !region) return -EINVAL;
    int dw = dr->r - dr->l, dh = dr->b - dr->t, sw = sr->r - sr->l, sh = sr->b - sr->t;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0;
    const uint8_t *Y = (const uint8_t *)src->base + src->offset;
    const uint8_t *C = Y + (size_t)src->w * src->h;
    int v420 = src->format == FMT_YCbCr_420_SP;
    int t = d->transform, a = d->alpha;
    /* the picture before the transform's 90 degree step is dw x dh, or dh x dw with ROT_90 */
    int pw = (t & T_ROT_90) ? dh : dw, ph = (t & T_ROT_90) ? dw : dh;
    uint32_t fx = udiv((uint32_t)sw << 16, (uint32_t)pw), fy = udiv((uint32_t)sh << 16, (uint32_t)ph);   /* sizes < 64K */
    uint8_t *out = (uint8_t *)dst->base + dst->offset;
    struct rect c;
    while (region->next(region, &c)) {
        if (c.l < dr->l) c.l = dr->l;
        if (c.t < dr->t) c.t = dr->t;
        if (c.r > dr->r) c.r = dr->r;
        if (c.b > dr->b) c.b = dr->b;
        if (c.l < 0) c.l = 0;
        if (c.t < 0) c.t = 0;
        if (c.r > (int)dst->w) c.r = (int)dst->w;
        if (c.b > (int)dst->h) c.b = (int)dst->h;
        for (int y = c.t; y < c.b; y++) {
            uint8_t *row = out + (size_t)y * dst->w * ob;
            for (int x = c.l; x < c.r; x++) {
                int u = x - dr->l, v = y - dr->t, px, py;
                /* undo the 90 degree step (clockwise: (px, py) lands at (ph - 1 - py, px)), then the flips */
                if (t & T_ROT_90) { px = v; py = dw - 1 - u; } else { px = u; py = v; }
                if (t & T_FLIP_H) px = pw - 1 - px;
                if (t & T_FLIP_V) py = ph - 1 - py;
                int sx = sr->l + (int)(((uint32_t)px * fx) >> 16), sy = sr->t + (int)(((uint32_t)py * fy) >> 16);
                int yy = Y[(size_t)sy * src->w + sx] - 16;
                const uint8_t *cc = C + (size_t)(v420 ? sy >> 1 : sy) * src->w + (sx & ~1);
                int cr = cc[0] - 128, cb = cc[1] - 128;
                int R = clampu8((298 * yy + 409 * cr + 128) >> 8);
                int G = clampu8((298 * yy - 100 * cb - 208 * cr + 128) >> 8);
                int B = clampu8((298 * yy + 516 * cb + 128) >> 8);
                if (dst->format == FMT_RGB_565) {
                    uint16_t *p = (uint16_t *)row + x;
                    if (a < 255) {
                        int r0 = (*p >> 11) << 3, g0 = ((*p >> 5) & 63) << 2, b0 = (*p & 31) << 3;
                        R = mix(R, r0, a); G = mix(G, g0, a); B = mix(B, b0, a);
                    }
                    *p = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
                } else {
                    uint8_t *p = row + (size_t)x * 4;
                    int ri = dst->format == FMT_BGRA_8888 ? 2 : 0, bi = 2 - ri;
                    if (a < 255) {
                        R = mix(R, p[ri], a); G = mix(G, p[1], a); B = mix(B, p[bi], a);
                    }
                    p[ri] = (uint8_t)R; p[1] = (uint8_t)G; p[bi] = (uint8_t)B; p[3] = 255;
                }
            }
        }
    }
    return 0;
}

static int dev_close(struct hw_device *d) { free(d); return 0; }
static int dev_open(const struct hw_module *m, const char *id, struct hw_device **out) {
    (void)id;
    struct device *d = (struct device *)calloc(1, sizeof *d);
    if (!d) return -12;
    d->common.tag = HARDWARE_DEVICE_TAG;
    d->common.module = (struct hw_module *)m;
    d->common.close = dev_close;
    d->set_parameter = set_parameter; d->get = get; d->blit = blit; d->stretch = stretch;
    d->alpha = 255;
    *out = &d->common;
    return 0;
}
static struct hw_module_methods methods = { dev_open };
EXPORT struct hw_module HMI = {
    HARDWARE_MODULE_TAG, 1, 0, "copybit", "AEmulator copybit (YUV to RGB)", "AEmulator", &methods, { 0 }
};
