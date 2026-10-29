/* The GLES 1 bridge of Android 1.x (guest1.c, host1.c): what both sides share. The calls are in gen1.py. It uses the
 * socket of the GLES 2/3 bridge (common.h); OP_HELLO with AEMU_GL1_PROTOCOL makes the connection a GLES 1 one. */
#ifndef AEMU_GL1_COMMON_H
#define AEMU_GL1_COMMON_H
#include <stdint.h>

#define AEMU_GL1_PROTOCOL 0x31454c47u      /* "GLE1" */
#ifndef NIL
#define NIL 0xffffffffu
#endif
#define BLOB1(n) ((n) == NIL ? 4u : 4u + (((n) + 3u) & ~3u))

/* Calls that are not GL entry points. "->" is what the host answers. */
enum {
    OP1_HELLO = 0x4000,     /* u32 AEMU_GL1_PROTOCOL -> u32 the same (0: refused) */
    OP1_CTX_CREATE,         /* u32 share -> u32 context (0: failed) */
    OP1_CTX_DESTROY,        /* u32 context */
    OP1_MAKE_CURRENT,       /* u32 context, u32 surface key, u32 w, u32 h -> u32 ok */
    OP1_RELEASE,            /* -> u32 ok */
    OP1_SURF_DESTROY,       /* u32 surface key */
    OP1_SWAP,               /* u32 w, u32 h, u32 format (1.x PixelFormat: 1 RGBA_8888, 2 RGBX_8888, 4 RGB_565, 5 BGRA_8888)
                               -> w*h pixels in that format, top row first, rows packed */
    OP1_DRAW,               /* see guest1.c draw() */
    OP1_GET_STRING,         /* u32 name -> u32 length (NIL: none), bytes padded to 4 */
    OP1_TEX_IMAGE,          /* glTexImage2D / glTexSubImage2D: u32 sub, 9 scalars as the call's, block (rows packed by the
                               unpack alignment the guest has, which the host has too) */
    OP1_READ_PIXELS,        /* u32 x, y, w, h, format, type, u32 bytes -> those bytes */
    OP1_GET,                /* u32 kind (0 boolean, 1 float, 2 integer, 3 fixed), u32 pname, u32 count -> count * 4 bytes */
    OP1_BUFFER_DATA,        /* u32 sub, u32 target, u32 offset (sub) or usage, block */
    OP1_FINISH,             /* -> u32 */
};
/* the arrays of a draw */
enum { A1_VERTEX, A1_COLOR, A1_NORMAL, A1_POINT_SIZE, A1_TEXCOORD0, A1_N = A1_TEXCOORD0 + 4 };

static inline uint32_t gl1_light_n(uint32_t pname) {
    switch (pname) {
    case 0x1200: case 0x1201: case 0x1202: case 0x1203: return 4;    /* AMBIENT, DIFFUSE, SPECULAR, POSITION */
    case 0x1204: return 3;                                           /* SPOT_DIRECTION */
    }
    return 1;
}
static inline uint32_t gl1_material_n(uint32_t pname) { return pname == 0x1601 /* SHININESS */ ? 1 : 4; }
/* how many values glGet* gives for a name (0x86A3: as many as 0x86A2 says, asked first) */
static inline uint32_t gl1_get_n(uint32_t pname) {
    switch (pname) {
    case 0x0BA6: case 0x0BA7: case 0x0BA8:                      /* MODELVIEW, PROJECTION, TEXTURE _MATRIX */
    case 0x898D: case 0x898E: case 0x898F:                      /* the same _FLOAT_AS_INT_BITS_OES */
        return 16;
    case 0x0BA2: case 0x0C10: case 0x0C22: case 0x0C23:         /* VIEWPORT, SCISSOR_BOX, COLOR_CLEAR_VALUE, COLOR_WRITEMASK */
    case 0x0B00: case 0x0B03: case 0x0B53: case 0x0B66:         /* CURRENT_COLOR, CURRENT_TEXTURE_COORDS, LIGHT_MODEL_AMBIENT, FOG_COLOR */
        return 4;
    case 0x0B02: case 0x8129:                                   /* CURRENT_NORMAL, POINT_DISTANCE_ATTENUATION */
        return 3;
    case 0x0B70: case 0x846D: case 0x846E: case 0x0B12: case 0x0B22: case 0x0D3A:
        return 2;   /* DEPTH_RANGE, ALIASED_POINT_SIZE_RANGE, ALIASED_LINE_WIDTH_RANGE, SMOOTH_POINT/LINE_*_RANGE, MAX_VIEWPORT_DIMS */
    }
    return 1;
}
/* bytes per pixel of a client format/type pair (glTexImage2D, glReadPixels) */
static inline uint32_t gl1_bpp(uint32_t format, uint32_t type) {
    if (type == 0x8363 || type == 0x8033 || type == 0x8034) return 2;   /* 5_6_5, 4_4_4_4, 5_5_5_1 */
    switch (format) {
    case 0x1906: case 0x1909: return 1;     /* ALPHA, LUMINANCE */
    case 0x190A: return 2;                  /* LUMINANCE_ALPHA */
    case 0x1907: return 3;                  /* RGB */
    }
    return 4;                               /* RGBA, BGRA */
}
/* bytes of a w x h image with rows aligned to align */
static inline uint32_t gl1_image_size(int w, int h, uint32_t bpp, int align) {
    if (w <= 0 || h <= 0) return 0;
    uint32_t row = (uint32_t)w * bpp, a = align > 0 ? (uint32_t)align : 4u, stride = (row + a - 1) / a * a;
    return stride * (uint32_t)(h - 1) + row;
}
#endif
