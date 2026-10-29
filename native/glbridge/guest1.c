/*
 * libhEGL.so (with libhGLESv1_CM.so, fwd1.c) - the "hardware" EGL/GLES 1 driver of Android 1.x, drawing on the
 * phone's GPU through the GLES bridge's host (host1.c).
 *
 * Android 1.x libEGL (Donut, and Qualcomm's variant of it) loads a software implementation (libagl.so) and a hardware
 * one (libhEGL.so + libhGLESv1_CM.so, or libhgl.so - this same library under that name, SELF_SO), lists the configs of both - hardware first - and dispatches each
 * context's calls to the implementation its config came from (opengl/libs/EGL/egl.cpp: MAKE_CONFIG(impl, index),
 * gl_hooks_t filled by dlsym of every name). Windows are egl_native_window_t (opengl/include/EGL/eglnatives.h): the
 * driver draws into base + offset (stride, format) and calls swapBuffers(), which flips and may move the buffer.
 *
 * Here a context is a GLES 1.1 context of the host on a pbuffer of the window's size; eglSwapBuffers reads the frame
 * back in the window's pixel format into the window's buffer and calls its swapBuffers(). Client vertex arrays go with
 * each draw (only the vertices it uses). Each drawing thread has its own connection.
 *
 * Built for Android 1.x bionic: libc, libdl, liblog; no TLS (pthread keys), the compiler's helpers linked in.
 */
#include <stdint.h>
#include <stddef.h>
#include "gl1_common.h"

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei, GLfixed, GLclampx, GLintptr, GLsizeiptr;
typedef float GLfloat, GLclampf;
typedef unsigned char GLboolean, GLubyte;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned short GLushort;
typedef void GLvoid;
typedef int EGLint;
typedef unsigned int EGLBoolean, EGLenum;
typedef void *EGLDisplay, *EGLConfig, *EGLSurface, *EGLContext, *EGLClientBuffer;
typedef void *NativeDisplayType, *NativeWindowType, *NativePixmapType;

extern void *malloc(size_t n);
extern void *calloc(size_t n, size_t s);
extern void *realloc(void *p, size_t n);
extern void free(void *p);
extern void *memcpy(void *d, const void *s, size_t n);
extern void *memset(void *d, int c, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern int socket(int domain, int type, int proto);
extern int connect(int fd, const void *addr, unsigned len);
extern long read(int fd, void *buf, size_t n);
extern long write(int fd, const void *buf, size_t n);
extern int close(int fd);
extern int *__errno(void);
typedef long pthread_key_t;
typedef struct { int value; } pthread_mutex_t;
extern int pthread_key_create(pthread_key_t *k, void (*d)(void *));
extern void *pthread_getspecific(pthread_key_t k);
extern int pthread_setspecific(pthread_key_t k, const void *v);
extern int pthread_mutex_lock(pthread_mutex_t *m);
extern int pthread_mutex_unlock(pthread_mutex_t *m);
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#define LOG(...) __android_log_print(4, "aemu-gl1", __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))
/* this library's own name: libhEGL.so (Qualcomm's loader, with libhGLESv1_CM.so) or libhgl.so (AOSP Cupcake/Donut
 * egl.cpp: load_driver("libhgl.so") takes EGL and GL from one library) - build1.sh builds both */
#ifndef SELF_SO
#define SELF_SO "libhEGL.so"
#endif

/* ---- EGL constants (EGL 1.2) ---- */
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_SUCCESS 0x3000
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D
#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_CONFIG_CAVEAT 0x3027
#define EGL_CONFIG_ID 0x3028
#define EGL_LEVEL 0x3029
#define EGL_MAX_PBUFFER_HEIGHT 0x302A
#define EGL_MAX_PBUFFER_PIXELS 0x302B
#define EGL_MAX_PBUFFER_WIDTH 0x302C
#define EGL_NATIVE_RENDERABLE 0x302D
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_SURFACE_TYPE 0x3033
#define EGL_TRANSPARENT_TYPE 0x3034
#define EGL_NONE 0x3038
#define EGL_BIND_TO_TEXTURE_RGB 0x3039
#define EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define EGL_MIN_SWAP_INTERVAL 0x303B
#define EGL_MAX_SWAP_INTERVAL 0x303C
#define EGL_LUMINANCE_SIZE 0x303D
#define EGL_ALPHA_MASK_SIZE 0x303E
#define EGL_COLOR_BUFFER_TYPE 0x303F
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_RGB_BUFFER 0x308E
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_CLIENT_APIS 0x308D
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_HORIZONTAL_RESOLUTION 0x3090
#define EGL_VERTICAL_RESOLUTION 0x3091
#define EGL_PIXEL_ASPECT_RATIO 0x3092
#define EGL_RENDER_BUFFER 0x3086
#define EGL_SWAP_BEHAVIOR 0x3093
#define EGL_BACK_BUFFER 0x3084
#define EGL_BUFFER_PRESERVED 0x3094
#define EGL_BUFFER_DESTROYED 0x3095
#define EGL_DONT_CARE ((EGLint)-1)
#define EGL_WINDOW_BIT 0x04
#define EGL_PBUFFER_BIT 0x01
#define EGL_OPENGL_ES_BIT 0x01
#define EGL_DISPLAY_SCALING 10000

/* ---- 1.x native window (Donut opengl/include/EGL/eglnatives.h) ---- */
struct egl_native_window_t {
    uint32_t magic, version, ident;
    int width, height, stride, format;
    intptr_t offset;
    uint32_t flags;
    float xdpi, ydpi, fps;
    intptr_t base;
    int fd;
    uint8_t memory_type, reserved_pad[3];
    int reserved[8];
    int vstride;
    void (*incRef)(struct egl_native_window_t *);
    void (*decRef)(struct egl_native_window_t *);
    uint32_t (*swapBuffers)(struct egl_native_window_t *);
    void (*reserved_proc_0)(void), (*reserved_proc_1)(void), (*reserved_proc_2)(void);
    void (*connect)(struct egl_native_window_t *);
    void (*disconnect)(struct egl_native_window_t *);
    void (*reserved_proc[11])(void);
    intptr_t oem[4];
};
#define NATIVE_WINDOW_MAGIC 0x600913

/* 1.x PixelFormat */
static int fmt_bpp(int f) { return f == 4 || f == 6 || f == 7 ? 2 : f == 3 ? 3 : 4; }

/* ---- configs ---- */
struct cfg { int r, g, b, a, depth, stencil, visual; };
static const struct cfg kCfg[] = {
    { 5, 6, 5, 0, 0, 0, 4 }, { 5, 6, 5, 0, 16, 0, 4 }, { 5, 6, 5, 0, 24, 8, 4 },
    { 8, 8, 8, 8, 0, 0, 1 }, { 8, 8, 8, 8, 16, 0, 1 }, { 8, 8, 8, 8, 24, 8, 1 },
};
#define NCFG ((int)(sizeof(kCfg) / sizeof(kCfg[0])))
static const struct cfg *cfg_of(EGLConfig c) { uintptr_t i = (uintptr_t)c; return i >= 1 && i <= (uintptr_t)NCFG ? &kCfg[i - 1] : 0; }

/* ---- objects ---- */
struct arr { int size, type, stride; const void *ptr; unsigned buf; };
struct ectx {
    uint32_t id;
    EGLConfig cfg;
    struct arr a[A1_N];
    unsigned enabled, array_buf, elem_buf;
    int client_tex, unpack_align, pack_align;
    char *str[4];
};
struct esurf {
    uint32_t key;
    int window;
    struct egl_native_window_t *win;
    int w, h;
    EGLConfig cfg;
};
struct ts {
    int fd, dead;
    uint8_t *buf;
    uint32_t n, cap;
    struct ectx *ctx;
    struct esurf *draw, *read;
    EGLint err;
    uint8_t *px; uint32_t px_cap;
};

static pthread_key_t g_key;
static int g_key_ok;
static pthread_mutex_t g_lock;
static uint32_t g_keys;
static int g_host = -1;     /* -1 unknown, 0 none, 1 there */

static void ts_free(void *p) {
    struct ts *t = (struct ts *)p;
    if (!t) return;
    if (t->fd >= 0) close(t->fd);
    free(t->buf); free(t->px); free(t);
}
static int wr_all(int fd, const void *p, size_t n) {
    const uint8_t *c = (const uint8_t *)p;
    while (n) { long k = write(fd, c, n); if (k < 0 && *__errno() == 4) continue; if (k <= 0) return 0; c += k; n -= (size_t)k; }
    return 1;
}
static int rd_all(int fd, void *p, size_t n) {
    uint8_t *c = (uint8_t *)p;
    while (n) { long k = read(fd, c, n); if (k < 0 && *__errno() == 4) continue; if (k <= 0) return 0; c += k; n -= (size_t)k; }
    return 1;
}
static int host_connect(void) {
    struct { unsigned short fam; char path[108]; } sa;
    memset(&sa, 0, sizeof sa);
    sa.fam = 1;
    const char *p = "/dev/socket/aemu_gl";
    memcpy(sa.path, p, strlen(p) + 1);
    int fd = socket(1, 1 | 02000000, 0);
    if (fd < 0) return -1;
    if (connect(fd, &sa, (unsigned)(2 + strlen(p) + 1)) < 0) { close(fd); return -1; }
    uint32_t m[3] = { OP1_HELLO, 4, AEMU_GL1_PROTOCOL }, r = 0;
    if (!wr_all(fd, m, sizeof m) || !rd_all(fd, &r, 4) || r != AEMU_GL1_PROTOCOL) { close(fd); return -1; }
    return fd;
}
static struct ts *T(void) {
    if (!g_key_ok) { pthread_mutex_lock(&g_lock); if (!g_key_ok) { pthread_key_create(&g_key, ts_free); g_key_ok = 1; } pthread_mutex_unlock(&g_lock); }
    struct ts *t = (struct ts *)pthread_getspecific(g_key);
    if (!t) {
        t = (struct ts *)calloc(1, sizeof *t);
        if (!t) return 0;
        t->fd = host_connect(); t->err = EGL_SUCCESS;
        if (t->fd < 0) { t->dead = 1; LOG("no GL host"); }
        pthread_setspecific(g_key, t);
    }
    return t;
}

/* ---- the stream ---- */
static int flush1(struct ts *t) {
    if (t->n && !t->dead && !wr_all(t->fd, t->buf, t->n)) { t->dead = 1; LOG("the GL host is gone"); }
    t->n = 0;
    return !t->dead;
}
static uint8_t *enc_t(struct ts *t, uint32_t op, uint32_t len) {
    if (!t || t->dead) return 0;
    if (t->n + 8 + len > t->cap) {
        if (t->n) flush1(t);
        if (8 + len > t->cap) {
            uint32_t c = 8 + len > (256u << 10) ? 8 + len : (256u << 10);
            uint8_t *b = (uint8_t *)realloc(t->buf, c);
            if (!b) return 0;
            t->buf = b; t->cap = c;
        }
    }
    uint8_t *q = t->buf + t->n;
    memcpy(q, &op, 4); memcpy(q + 4, &len, 4);
    t->n += 8 + len;
    return q + 8;
}
/* GL calls go only with a context of ours current on this thread */
static uint8_t *enc1(uint32_t op, uint32_t len) {
    struct ts *t = (struct ts *)(g_key_ok ? pthread_getspecific(g_key) : 0);
    if (!t || !t->ctx) return 0;
    return enc_t(t, op, len);
}
static inline uint8_t *put32(uint8_t *q, uint32_t v) { memcpy(q, &v, 4); return q + 4; }
static inline uint8_t *putf(uint8_t *q, float v) { memcpy(q, &v, 4); return q + 4; }
static uint8_t *putblob(uint8_t *q, const void *p, uint32_t n) {
    if (n == NIL || !p) return put32(q, NIL);
    q = put32(q, n);
    memcpy(q, p, n);
    uint32_t pad = (4 - (n & 3)) & 3;
    memset(q + n, 0, pad);
    return q + n + pad;
}
static struct ts *cur(void) { return (struct ts *)(g_key_ok ? pthread_getspecific(g_key) : 0); }
static int call1(void) { struct ts *t = cur(); return t && flush1(t); }
static void get1(void *p, uint32_t n) {
    struct ts *t = cur();
    uint8_t tmp[64];
    if (!t || t->dead) { if (p) memset(p, 0, n); return; }
    if (p) { if (!rd_all(t->fd, p, n)) t->dead = 1; return; }
    while (n) { uint32_t k = n > sizeof tmp ? sizeof tmp : n; if (!rd_all(t->fd, tmp, k)) { t->dead = 1; return; } n -= k; }
}
static uint32_t get1_32(void) { uint32_t v = 0; get1(&v, 4); return v; }

/* ---- what the guest notes of calls it sends (k1_*) ---- */
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
/* The contents of element buffers, kept here: a draw from one needs the range of vertices its indices use. */
struct shadow { unsigned id; uint8_t *data; uint32_t n; };
static struct shadow g_sh[64];
static struct shadow *sh_find(unsigned id, int make) {
    struct shadow *free_one = 0;
    for (int i = 0; i < 64; i++) { if (g_sh[i].id == id) return &g_sh[i]; if (!g_sh[i].id && !free_one) free_one = &g_sh[i]; }
    if (!make || !free_one) return 0;
    free_one->id = id; free_one->data = 0; free_one->n = 0;
    return free_one;
}
static void k1_glBindBuffer(GLenum target, GLuint buffer) {
    struct ts *t = cur();
    if (!t || !t->ctx) return;
    if (target == GL_ARRAY_BUFFER) t->ctx->array_buf = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) t->ctx->elem_buf = buffer;
}
static void k1_glDeleteBuffers(GLsizei n, const GLuint *ids) {
    struct ts *t = cur();
    if (!t || !t->ctx || !ids) return;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < n; i++) {
        struct shadow *s = sh_find(ids[i], 0);
        if (s) { free(s->data); s->id = 0; s->data = 0; s->n = 0; }
        if (t->ctx->array_buf == ids[i]) t->ctx->array_buf = 0;
        if (t->ctx->elem_buf == ids[i]) t->ctx->elem_buf = 0;
        for (int k = 0; k < A1_N; k++) if (t->ctx->a[k].buf == ids[i]) t->ctx->a[k].buf = 0;
    }
    pthread_mutex_unlock(&g_lock);
}
static void k1_glPixelStorei(GLenum pname, GLint param) {
    struct ts *t = cur();
    if (!t || !t->ctx) return;
    if (pname == GL_UNPACK_ALIGNMENT) t->ctx->unpack_align = param;
    else if (pname == GL_PACK_ALIGNMENT) t->ctx->pack_align = param;
}

#include "gl1_api.h"
#include "g1_enc.h"

/* ---- GL by hand ---- */
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_POINT_SIZE_ARRAY_OES 0x8B9C
#define GL_TEXTURE0 0x84C0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1

static struct ectx *X(void) { struct ts *t = cur(); return t ? t->ctx : 0; }
static int arr_of(GLenum cap, struct ectx *x) {
    switch (cap) {
    case GL_VERTEX_ARRAY: return A1_VERTEX;
    case GL_NORMAL_ARRAY: return A1_NORMAL;
    case GL_COLOR_ARRAY: return A1_COLOR;
    case GL_POINT_SIZE_ARRAY_OES: return A1_POINT_SIZE;
    case GL_TEXTURE_COORD_ARRAY: return A1_TEXCOORD0 + (x ? x->client_tex : 0);
    }
    return -1;
}
static void set_arr(int i, GLint size, GLenum type, GLsizei stride, const void *ptr) {
    struct ectx *x = X();
    if (!x) return;
    x->a[i].size = size; x->a[i].type = (int)type; x->a[i].stride = stride; x->a[i].ptr = ptr; x->a[i].buf = x->array_buf;
}
EXPORT void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_arr(A1_VERTEX, size, type, stride, p); }
EXPORT void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_arr(A1_COLOR, size, type, stride, p); }
EXPORT void glNormalPointer(GLenum type, GLsizei stride, const void *p) { set_arr(A1_NORMAL, 3, type, stride, p); }
EXPORT void glPointSizePointerOES(GLenum type, GLsizei stride, const void *p) { set_arr(A1_POINT_SIZE, 1, type, stride, p); }
EXPORT void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
    struct ectx *x = X();
    if (x) set_arr(A1_TEXCOORD0 + x->client_tex, size, type, stride, p);
}
EXPORT void glClientActiveTexture(GLenum tex) {
    struct ectx *x = X();
    if (x && tex >= GL_TEXTURE0 && tex < GL_TEXTURE0 + 4) x->client_tex = (int)(tex - GL_TEXTURE0);
}
EXPORT void glEnableClientState(GLenum cap) { struct ectx *x = X(); int i = arr_of(cap, x); if (x && i >= 0) x->enabled |= 1u << i; }
EXPORT void glDisableClientState(GLenum cap) { struct ectx *x = X(); int i = arr_of(cap, x); if (x && i >= 0) x->enabled &= ~(1u << i); }
EXPORT GLboolean glIsEnabled(GLenum cap) {
    struct ectx *x = X();
    int i = arr_of(cap, x);
    if (i >= 0) return x && (x->enabled >> i) & 1;
    uint8_t *q = enc1(IDX1_glIsEnabled, 4);
    if (!q) return 0;
    put32(q, cap);
    if (!call1()) return 0;
    return (GLboolean)get1_32();
}
EXPORT void glGetPointerv(GLenum pname, void **params) {
    struct ectx *x = X();
    if (!params) return;
    *params = 0;
    if (!x) return;
    switch (pname) {
    case 0x808E: *params = (void *)x->a[A1_VERTEX].ptr; break;      /* VERTEX_ARRAY_POINTER */
    case 0x808F: *params = (void *)x->a[A1_NORMAL].ptr; break;
    case 0x8090: *params = (void *)x->a[A1_COLOR].ptr; break;
    case 0x8092: *params = (void *)x->a[A1_TEXCOORD0 + x->client_tex].ptr; break;
    case 0x898C: *params = (void *)x->a[A1_POINT_SIZE].ptr; break;
    }
}

static uint32_t type_size(int type) { return type == 0x1400 || type == 0x1401 ? 1 : type == 0x1402 || type == 0x1403 ? 2 : 4; }
/* A draw: u32 mode, count, first, index type (0: arrays), index offset (NIL: the indices follow as a block),
 * [indices], u32 the guest's GL_ARRAY_BUFFER binding (restored after), u32 mask of the enabled arrays, then per
 * enabled array: u32 size, type, stride, buffer, first vertex, and the buffer's offset or a block of the vertices
 * from the first one on. */
static void draw(GLenum mode, GLsizei count, GLint first, GLenum itype, const void *indices) {
    struct ts *t = cur();
    struct ectx *x = t ? t->ctx : 0;
    if (!x || count <= 0) return;
    uint32_t lo = (uint32_t)first, hi = (uint32_t)first + (uint32_t)count - 1, isz = 0;
    const uint8_t *ip = 0;
    if (itype) {
        isz = itype == 0x1401 ? 1 : itype == 0x1405 ? 4 : 2;
        if (x->elem_buf) {
            pthread_mutex_lock(&g_lock);
            struct shadow *s = sh_find(x->elem_buf, 0);
            uintptr_t off = (uintptr_t)indices;
            if (s && s->data && off + (uintptr_t)count * isz <= s->n) ip = s->data + off;
            pthread_mutex_unlock(&g_lock);
        } else ip = (const uint8_t *)indices;
        lo = 0xffffffffu; hi = 0;
        if (ip) for (GLsizei i = 0; i < count; i++) {
            uint32_t v = isz == 1 ? ip[i] : isz == 2 ? ((const uint16_t *)ip)[i] : ((const uint32_t *)ip)[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (lo > hi) { lo = 0; hi = 0; }
    }
    uint32_t len = 5 * 4 + 8, n[A1_N];
    if (itype && !x->elem_buf) len += BLOB1(ip ? (uint32_t)count * isz : NIL);
    for (int i = 0; i < A1_N; i++) {
        n[i] = 0;
        if (!((x->enabled >> i) & 1)) continue;
        struct arr *a = &x->a[i];
        uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        n[i] = a->buf || !a->ptr ? NIL : (hi - lo) * es + (uint32_t)a->size * type_size(a->type);
        len += 5 * 4 + (a->buf || !a->ptr ? 4 : BLOB1(n[i]));
    }
    uint8_t *q = enc_t(t, OP1_DRAW, len);
    if (!q) return;
    q = put32(q, mode); q = put32(q, (uint32_t)count); q = put32(q, (uint32_t)first); q = put32(q, itype);
    if (itype && !x->elem_buf) { q = put32(q, NIL); q = putblob(q, ip, ip ? (uint32_t)count * isz : NIL); }
    else q = put32(q, (uint32_t)(uintptr_t)indices);
    q = put32(q, x->array_buf); q = put32(q, x->enabled);
    for (int i = 0; i < A1_N; i++) {
        if (!((x->enabled >> i) & 1)) continue;
        struct arr *a = &x->a[i];
        uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        q = put32(q, (uint32_t)a->size); q = put32(q, (uint32_t)a->type); q = put32(q, (uint32_t)a->stride);
        q = put32(q, a->buf); q = put32(q, lo);
        if (a->buf || !a->ptr) q = put32(q, (uint32_t)(uintptr_t)a->ptr);
        else q = putblob(q, (const uint8_t *)a->ptr + (size_t)lo * es, n[i]);
    }
}
EXPORT void glDrawArrays(GLenum mode, GLint first, GLsizei count) { draw(mode, count, first, 0, 0); }
EXPORT void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) { draw(mode, count, 0, type ? type : 0x1403, indices); }

EXPORT void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    struct ectx *x = X();
    if (!x || size < 0) return;
    unsigned id = target == GL_ARRAY_BUFFER ? x->array_buf : target == GL_ELEMENT_ARRAY_BUFFER ? x->elem_buf : 0;
    if (target == GL_ELEMENT_ARRAY_BUFFER && id) {
        pthread_mutex_lock(&g_lock);
        struct shadow *s = sh_find(id, 1);
        if (s) {
            uint8_t *d = (uint8_t *)realloc(s->data, (size_t)size ? (size_t)size : 1);
            if (d) { s->data = d; s->n = (uint32_t)size; if (data) memcpy(d, data, (size_t)size); else memset(d, 0, (size_t)size); }
        }
        pthread_mutex_unlock(&g_lock);
    }
    uint8_t *q = enc1(OP1_BUFFER_DATA, 12 + BLOB1(data ? (uint32_t)size : NIL) + 4);
    if (!q) return;
    q = put32(q, 0); q = put32(q, target); q = put32(q, usage); q = put32(q, (uint32_t)size);
    putblob(q, data, data ? (uint32_t)size : NIL);
}
EXPORT void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    struct ectx *x = X();
    if (!x || size <= 0 || !data) return;
    if (target == GL_ELEMENT_ARRAY_BUFFER && x->elem_buf) {
        pthread_mutex_lock(&g_lock);
        struct shadow *s = sh_find(x->elem_buf, 0);
        if (s && s->data && offset >= 0 && (uint32_t)offset + (uint32_t)size <= s->n) memcpy(s->data + offset, data, (size_t)size);
        pthread_mutex_unlock(&g_lock);
    }
    uint8_t *q = enc1(OP1_BUFFER_DATA, 12 + BLOB1((uint32_t)size) + 4);
    if (!q) return;
    q = put32(q, 1); q = put32(q, target); q = put32(q, (uint32_t)offset); q = put32(q, (uint32_t)size);
    putblob(q, data, (uint32_t)size);
}

static void tex_image(int sub, GLenum target, GLint level, GLint a, GLint b, GLint c, GLsizei w, GLsizei h, GLint d,
                      GLenum format, GLenum type, const void *pixels) {
    struct ectx *x = X();
    if (!x) return;
    uint32_t n = pixels ? gl1_image_size(w, h, gl1_bpp(format, type), x->unpack_align ? x->unpack_align : 4) : NIL;
    uint8_t *q = enc1(OP1_TEX_IMAGE, 4 * 11 + BLOB1(n));
    if (!q) return;
    q = put32(q, (uint32_t)sub); q = put32(q, target); q = put32(q, (uint32_t)level); q = put32(q, (uint32_t)a);
    q = put32(q, (uint32_t)b); q = put32(q, (uint32_t)c); q = put32(q, (uint32_t)w); q = put32(q, (uint32_t)h);
    q = put32(q, (uint32_t)d); q = put32(q, format); q = put32(q, type);
    putblob(q, pixels, n);
}
/* (glTexImage2D: internal format, w, h, border; glTexSubImage2D: x, y, w, h) */
EXPORT void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border,
                         GLenum format, GLenum type, const void *pixels) {
    tex_image(0, target, level, internalformat, 0, 0, w, h, border, format, type, pixels);
}
EXPORT void glTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum format,
                            GLenum type, const void *pixels) {
    tex_image(1, target, level, xo, yo, 0, w, h, 0, format, type, pixels);
}
EXPORT void glReadPixels(GLint x0, GLint y0, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels) {
    struct ectx *x = X();
    if (!x) return;
    uint32_t n = gl1_image_size(w, h, gl1_bpp(format, type), x->pack_align ? x->pack_align : 4);
    uint8_t *q = enc1(OP1_READ_PIXELS, 28);
    if (!q) return;
    q = put32(q, (uint32_t)x0); q = put32(q, (uint32_t)y0); q = put32(q, (uint32_t)w); q = put32(q, (uint32_t)h);
    q = put32(q, format); q = put32(q, type); put32(q, n);
    if (call1()) get1(pixels, n);
}
static int get_local(GLenum pname, GLint *v) {
    struct ectx *x = X();
    if (!x) return 0;
    switch (pname) {
    case GL_CLIENT_ACTIVE_TEXTURE: *v = GL_TEXTURE0 + x->client_tex; return 1;
    case 0x807A: *v = x->a[A1_VERTEX].size; return 1;      /* VERTEX_ARRAY_SIZE */
    case 0x807B: *v = x->a[A1_VERTEX].type; return 1;
    case 0x807C: *v = x->a[A1_VERTEX].stride; return 1;
    case 0x807E: *v = x->a[A1_NORMAL].type; return 1;      /* NORMAL_ARRAY_TYPE */
    case 0x807F: *v = x->a[A1_NORMAL].stride; return 1;
    case 0x8081: *v = x->a[A1_COLOR].size; return 1;       /* COLOR_ARRAY_SIZE */
    case 0x8082: *v = x->a[A1_COLOR].type; return 1;
    case 0x8083: *v = x->a[A1_COLOR].stride; return 1;
    case 0x8088: *v = x->a[A1_TEXCOORD0 + x->client_tex].size; return 1;
    case 0x8089: *v = x->a[A1_TEXCOORD0 + x->client_tex].type; return 1;
    case 0x808A: *v = x->a[A1_TEXCOORD0 + x->client_tex].stride; return 1;
    case 0x0CF5: *v = x->unpack_align ? x->unpack_align : 4; return 1;
    case 0x0D05: *v = x->pack_align ? x->pack_align : 4; return 1;
    }
    int i = arr_of(pname, x);
    if (i >= 0) { *v = (x->enabled >> i) & 1; return 1; }
    return 0;
}
static void get_any(uint32_t kind, GLenum pname, void *out) {
    GLint l;
    if (get_local(pname, &l)) {
        if (kind == 0) *(GLboolean *)out = l != 0;
        else if (kind == 1) *(GLfloat *)out = (GLfloat)l;
        else if (kind == 3 && pname != GL_CLIENT_ACTIVE_TEXTURE) *(GLfixed *)out = l << 16;
        else *(GLint *)out = l;
        return;
    }
    uint32_t cnt = gl1_get_n(pname);
    if (pname == 0x86A3) { GLint k = 0; get_any(2, 0x86A2, &k); cnt = k > 0 ? (uint32_t)k : 0; }
    uint8_t *q = enc1(OP1_GET, 12);
    if (!q) return;
    q = put32(q, kind); q = put32(q, pname); put32(q, cnt);
    if (!call1()) return;
    if (kind == 0) {        /* (booleans come as 4 bytes each) */
        for (uint32_t i = 0; i < cnt; i++) ((GLboolean *)out)[i] = get1_32() != 0;
    } else get1(out, cnt * 4);
}
EXPORT void glGetBooleanv(GLenum pname, GLboolean *v) { if (v) get_any(0, pname, v); }
EXPORT void glGetFloatv(GLenum pname, GLfloat *v) { if (v) get_any(1, pname, v); }
EXPORT void glGetIntegerv(GLenum pname, GLint *v) { if (v) get_any(2, pname, v); }
EXPORT void glGetFixedv(GLenum pname, GLfixed *v) { if (v) get_any(3, pname, v); }
EXPORT void glFinish(void) {
    if (!enc1(OP1_FINISH, 0)) return;
    if (call1()) get1_32();
}
EXPORT const GLubyte *glGetString(GLenum name) {
    struct ectx *x = X();
    int i = name == 0x1F00 ? 0 : name == 0x1F01 ? 1 : name == 0x1F02 ? 2 : name == 0x1F03 ? 3 : -1;
    if (!x || i < 0) return 0;
    if (x->str[i]) return (const GLubyte *)x->str[i];
    uint8_t *q = enc1(OP1_GET_STRING, 4);
    if (!q) return 0;
    put32(q, name);
    if (!call1()) return 0;
    uint32_t n = get1_32();
    if (n == NIL) return 0;
    char *s = (char *)malloc(n + 4);
    if (!s) { get1(0, (n + 3) & ~3u); return 0; }
    get1(s, (n + 3) & ~3u);
    s[n] = 0;
    x->str[i] = s;
    return (const GLubyte *)s;
}

/* ---- EGL ---- */
static int seterr(EGLint e) { struct ts *t = T(); if (t) t->err = e; return 0; }
#define DPY ((EGLDisplay)1)
static int dpy_ok(EGLDisplay d) { return d == DPY; }

EXPORT EGLint eglGetError(void) { struct ts *t = T(); EGLint e = t ? t->err : EGL_SUCCESS; if (t) t->err = EGL_SUCCESS; return e; }
EXPORT EGLDisplay eglGetDisplay(NativeDisplayType d) {
    (void)d;
    if (g_host < 0) { struct ts *t = T(); g_host = t && !t->dead ? 1 : 0; }
    return g_host ? DPY : 0;
}
EXPORT EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor) {
    if (!dpy_ok(d) || g_host != 1) return seterr(EGL_BAD_DISPLAY);
    if (major) *major = 1;
    if (minor) *minor = 2;
    return EGL_TRUE;
}
EXPORT EGLBoolean eglTerminate(EGLDisplay d) { return dpy_ok(d) ? EGL_TRUE : seterr(EGL_BAD_DISPLAY); }
EXPORT const char *eglQueryString(EGLDisplay d, EGLint name) {
    if (!dpy_ok(d)) { seterr(EGL_BAD_DISPLAY); return 0; }
    switch (name) {
    case EGL_VENDOR: return "AEmulator";
    case EGL_VERSION: return "1.2 AEmulator GPU bridge";
    case EGL_EXTENSIONS: return "";
    case EGL_CLIENT_APIS: return "OpenGL ES";
    }
    seterr(EGL_BAD_PARAMETER);
    return 0;
}
static EGLint cfg_attr(const struct cfg *c, int id, EGLint a) {
    switch (a) {
    case EGL_BUFFER_SIZE: return c->r + c->g + c->b + c->a;
    case EGL_RED_SIZE: return c->r;
    case EGL_GREEN_SIZE: return c->g;
    case EGL_BLUE_SIZE: return c->b;
    case EGL_ALPHA_SIZE: return c->a;
    case EGL_DEPTH_SIZE: return c->depth;
    case EGL_STENCIL_SIZE: return c->stencil;
    case EGL_CONFIG_ID: return id;
    case EGL_NATIVE_VISUAL_ID: return c->visual;
    case EGL_SURFACE_TYPE: return EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
    case EGL_RENDERABLE_TYPE: return EGL_OPENGL_ES_BIT;
    case EGL_CONFIG_CAVEAT: return EGL_NONE;
    case EGL_NATIVE_RENDERABLE: return EGL_TRUE;
    case EGL_MAX_PBUFFER_WIDTH: case EGL_MAX_PBUFFER_HEIGHT: return 2048;
    case EGL_MAX_PBUFFER_PIXELS: return 2048 * 2048;
    case EGL_MIN_SWAP_INTERVAL: return 0;
    case EGL_MAX_SWAP_INTERVAL: return 1;
    case EGL_TRANSPARENT_TYPE: return EGL_NONE;
    case EGL_COLOR_BUFFER_TYPE: return EGL_RGB_BUFFER;
    }
    return 0;
}
EXPORT EGLBoolean eglGetConfigs(EGLDisplay d, EGLConfig *cs, EGLint size, EGLint *num) {
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!num) return seterr(EGL_BAD_PARAMETER);
    if (!cs) { *num = NCFG; return EGL_TRUE; }
    int n = 0;
    for (int i = 0; i < NCFG && n < size; i++) cs[n++] = (EGLConfig)(uintptr_t)(i + 1);
    *num = n;
    return EGL_TRUE;
}
EXPORT EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *attr, EGLConfig *cs, EGLint size, EGLint *num) {
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!num) return seterr(EGL_BAD_PARAMETER);
    int want_r = 0, want_g = 0, want_b = 0, want_a = 0, ok[NCFG], order[NCFG], n = 0;
    for (int i = 0; i < NCFG; i++) ok[i] = 1;
    for (const EGLint *a = attr; a && a[0] != EGL_NONE; a += 2) {
        EGLint k = a[0], v = a[1];
        if (v == EGL_DONT_CARE) continue;
        for (int i = 0; i < NCFG; i++) {
            EGLint have = cfg_attr(&kCfg[i], i + 1, k);
            switch (k) {
            case EGL_BUFFER_SIZE: case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE:
            case EGL_DEPTH_SIZE: case EGL_STENCIL_SIZE:
                if (have < v) ok[i] = 0;
                break;
            case EGL_CONFIG_ID: case EGL_NATIVE_VISUAL_ID: if (have != v) ok[i] = 0; break;
            case EGL_SURFACE_TYPE: case EGL_RENDERABLE_TYPE: if ((have & v) != v) ok[i] = 0; break;
            case EGL_SAMPLES: case EGL_SAMPLE_BUFFERS: case EGL_LEVEL: if (v > 0) ok[i] = 0; break;
            }
        }
        if (k == EGL_RED_SIZE) want_r = v > 0;
        if (k == EGL_GREEN_SIZE) want_g = v > 0;
        if (k == EGL_BLUE_SIZE) want_b = v > 0;
        if (k == EGL_ALPHA_SIZE) want_a = v > 0;
    }
    /* EGL's order: more bits of the asked colours first, then the smaller buffer, depth, stencil */
    for (int i = 0; i < NCFG; i++) if (ok[i]) order[n++] = i;
    for (int i = 1; i < n; i++) for (int j = i; j > 0; j--) {
        const struct cfg *p = &kCfg[order[j - 1]], *q = &kCfg[order[j]];
        int cp = want_r * p->r + want_g * p->g + want_b * p->b + want_a * p->a;
        int cq = want_r * q->r + want_g * q->g + want_b * q->b + want_a * q->a;
        int bp = p->r + p->g + p->b + p->a, bq = q->r + q->g + q->b + q->a;
        int swap = cq > cp || (cq == cp && (bq < bp || (bq == bp && (q->depth < p->depth || (q->depth == p->depth && q->stencil < p->stencil)))));
        if (!swap) break;
        int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
    }
    if (!cs) { *num = n; return EGL_TRUE; }
    int k = 0;
    for (int i = 0; i < n && k < size; i++) cs[k++] = (EGLConfig)(uintptr_t)(order[i] + 1);
    *num = k;
    return EGL_TRUE;
}
EXPORT EGLBoolean eglGetConfigAttrib(EGLDisplay d, EGLConfig c, EGLint a, EGLint *v) {
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    const struct cfg *f = cfg_of(c);
    if (!f) return seterr(EGL_BAD_CONFIG);
    if (v) *v = cfg_attr(f, (int)(uintptr_t)c, a);
    return EGL_TRUE;
}
static struct esurf *surf_new(EGLConfig c, int window) {
    struct esurf *s = (struct esurf *)calloc(1, sizeof *s);
    if (!s) return 0;
    pthread_mutex_lock(&g_lock); s->key = ++g_keys; pthread_mutex_unlock(&g_lock);
    s->window = window; s->cfg = c;
    return s;
}
EXPORT EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c, NativeWindowType w, const EGLint *attr) {
    (void)attr;
    struct egl_native_window_t *win = (struct egl_native_window_t *)w;
    if (!dpy_ok(d)) { seterr(EGL_BAD_DISPLAY); return 0; }
    if (!cfg_of(c)) { seterr(EGL_BAD_CONFIG); return 0; }
    if (!win || win->magic != NATIVE_WINDOW_MAGIC) { seterr(EGL_BAD_NATIVE_WINDOW); return 0; }
    struct esurf *s = surf_new(c, 1);
    if (!s) { seterr(EGL_BAD_ALLOC); return 0; }
    s->win = win; s->w = win->width; s->h = win->height;
    return s;
}
EXPORT EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint *attr) {
    if (!dpy_ok(d)) { seterr(EGL_BAD_DISPLAY); return 0; }
    if (!cfg_of(c)) { seterr(EGL_BAD_CONFIG); return 0; }
    struct esurf *s = surf_new(c, 0);
    if (!s) { seterr(EGL_BAD_ALLOC); return 0; }
    for (const EGLint *a = attr; a && a[0] != EGL_NONE; a += 2) {
        if (a[0] == EGL_WIDTH) s->w = a[1];
        else if (a[0] == EGL_HEIGHT) s->h = a[1];
    }
    return s;
}
EXPORT EGLSurface eglCreatePixmapSurface(EGLDisplay d, EGLConfig c, NativePixmapType p, const EGLint *attr) {
    (void)d; (void)c; (void)p; (void)attr;
    seterr(EGL_BAD_MATCH);      /* (no config of this driver has EGL_PIXMAP_BIT) */
    return 0;
}
EXPORT EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface surface) {
    struct esurf *s = (struct esurf *)surface;
    struct ts *t = T();
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!s) return seterr(EGL_BAD_SURFACE);
    if (t && !t->dead) {
        uint8_t *q = enc_t(t, OP1_SURF_DESTROY, 4);
        if (q) put32(q, s->key);
        if (t->draw == s) t->draw = 0;
        if (t->read == s) t->read = 0;
        flush1(t);
    }
    free(s);
    return EGL_TRUE;
}
static void surf_size(struct esurf *s) { if (s->window && s->win) { s->w = s->win->width; s->h = s->win->height; } }
EXPORT EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface surface, EGLint a, EGLint *v) {
    struct esurf *s = (struct esurf *)surface;
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!s) return seterr(EGL_BAD_SURFACE);
    surf_size(s);
    EGLint r;
    switch (a) {
    case EGL_WIDTH: r = s->w; break;
    case EGL_HEIGHT: r = s->h; break;
    case EGL_CONFIG_ID: r = (EGLint)(uintptr_t)s->cfg; break;
    case EGL_HORIZONTAL_RESOLUTION: r = s->win ? (EGLint)(s->win->xdpi * EGL_DISPLAY_SCALING / 25.4f) : EGL_DONT_CARE; break;
    case EGL_VERTICAL_RESOLUTION: r = s->win ? (EGLint)(s->win->ydpi * EGL_DISPLAY_SCALING / 25.4f) : EGL_DONT_CARE; break;
    case EGL_PIXEL_ASPECT_RATIO: r = EGL_DISPLAY_SCALING; break;
    case EGL_RENDER_BUFFER: r = EGL_BACK_BUFFER; break;
    case EGL_SWAP_BEHAVIOR: r = EGL_BUFFER_PRESERVED; break;
    default: return seterr(EGL_BAD_ATTRIBUTE);
    }
    if (v) *v = r;
    return EGL_TRUE;
}
EXPORT EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attr) {
    (void)attr;
    struct ts *t = T();
    if (!dpy_ok(d)) { seterr(EGL_BAD_DISPLAY); return 0; }
    if (!cfg_of(c)) { seterr(EGL_BAD_CONFIG); return 0; }
    if (!t || t->dead) { seterr(EGL_BAD_ALLOC); return 0; }
    struct ectx *x = (struct ectx *)calloc(1, sizeof *x);
    if (!x) { seterr(EGL_BAD_ALLOC); return 0; }
    uint8_t *q = enc_t(t, OP1_CTX_CREATE, 4);
    if (q) put32(q, share ? ((struct ectx *)share)->id : 0);
    uint32_t id = 0;
    if (q && flush1(t) && rd_all(t->fd, &id, 4) && id) {
        x->id = id; x->cfg = c; x->unpack_align = 4; x->pack_align = 4;
        return x;
    }
    free(x);
    seterr(EGL_BAD_ALLOC);
    return 0;
}
EXPORT EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext ctx) {
    struct ectx *x = (struct ectx *)ctx;
    struct ts *t = T();
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!x) return seterr(EGL_BAD_CONTEXT);
    if (t && !t->dead) {
        uint8_t *q = enc_t(t, OP1_CTX_DESTROY, 4);
        if (q) put32(q, x->id);
        flush1(t);
        if (t->ctx == x) t->ctx = 0;
    }
    for (int i = 0; i < 4; i++) free(x->str[i]);
    free(x);
    return EGL_TRUE;
}
EXPORT EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    struct ts *t = T();
    struct ectx *x = (struct ectx *)ctx;
    struct esurf *s = (struct esurf *)draw;
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!t || t->dead) return seterr(EGL_BAD_ALLOC);
    uint32_t ok = 0;
    if (!x) {
        uint8_t *q = enc_t(t, OP1_RELEASE, 0);
        if (q && flush1(t)) rd_all(t->fd, &ok, 4);
        t->ctx = 0; t->draw = t->read = 0;
        return EGL_TRUE;
    }
    if (s) surf_size(s);
    uint8_t *q = enc_t(t, OP1_MAKE_CURRENT, 16);
    if (!q) return seterr(EGL_BAD_ALLOC);
    q = put32(q, x->id); q = put32(q, s ? s->key : 0); q = put32(q, s ? (uint32_t)s->w : 0); put32(q, s ? (uint32_t)s->h : 0);
    if (!flush1(t) || !rd_all(t->fd, &ok, 4) || !ok) return seterr(EGL_BAD_MATCH);
    t->ctx = x; t->draw = s; t->read = (struct esurf *)read;
    return EGL_TRUE;
}
EXPORT EGLContext eglGetCurrentContext(void) { struct ts *t = T(); return t ? t->ctx : 0; }
EXPORT EGLSurface eglGetCurrentSurface(EGLint which) { struct ts *t = T(); return !t ? 0 : which == 0x3059 /* EGL_READ */ ? t->read : t->draw; }
EXPORT EGLDisplay eglGetCurrentDisplay(void) { struct ts *t = T(); return t && t->ctx ? DPY : 0; }
EXPORT EGLBoolean eglQueryContext(EGLDisplay d, EGLContext ctx, EGLint a, EGLint *v) {
    struct ectx *x = (struct ectx *)ctx;
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!x) return seterr(EGL_BAD_CONTEXT);
    if (a != EGL_CONFIG_ID) return seterr(EGL_BAD_ATTRIBUTE);
    if (v) *v = (EGLint)(uintptr_t)x->cfg;
    return EGL_TRUE;
}
/* The frame: read back in the window's format into the window's buffer, then the window's own swapBuffers() */
EXPORT EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface surface) {
    struct ts *t = T();
    struct esurf *s = (struct esurf *)surface;
    if (!dpy_ok(d)) return seterr(EGL_BAD_DISPLAY);
    if (!s) return seterr(EGL_BAD_SURFACE);
    if (!s->window || !s->win || !t || !t->ctx || t->draw != s) return EGL_TRUE;
    struct egl_native_window_t *w = s->win;
    int fmt = w->format, bpp = fmt_bpp(fmt), ww = s->w, hh = s->h;
    if (ww > w->width) ww = w->width;
    if (hh > w->height) hh = w->height;
    uint32_t row = (uint32_t)(ww > 0 ? ww : 0) * (uint32_t)bpp, need = row * (uint32_t)(hh > 0 ? hh : 0);
    uint8_t *q = enc_t(t, OP1_SWAP, 12);
    if (!q) return seterr(EGL_BAD_ALLOC);
    q = put32(q, (uint32_t)ww); q = put32(q, (uint32_t)hh); put32(q, (uint32_t)fmt);
    if (!flush1(t)) return seterr(EGL_BAD_ALLOC);
    uint8_t *dst = (uint8_t *)(w->base + w->offset);
    uint32_t dstride = (uint32_t)w->stride * (uint32_t)bpp;
    if (dstride == row) { if (!rd_all(t->fd, dst, need)) t->dead = 1; }
    else for (int y = 0; y < hh; y++) if (!rd_all(t->fd, dst + (size_t)y * dstride, row)) { t->dead = 1; break; }
    if (w->swapBuffers) w->swapBuffers(w);
    /* (the window may have changed its size: the host's buffer follows at the next make-current or swap) */
    if (w->width != s->w || w->height != s->h) {
        s->w = w->width; s->h = w->height;
        uint8_t *m = enc_t(t, OP1_MAKE_CURRENT, 16);
        uint32_t ok = 0;
        if (m) { m = put32(m, t->ctx->id); m = put32(m, s->key); m = put32(m, (uint32_t)s->w); put32(m, (uint32_t)s->h); }
        if (m && flush1(t)) rd_all(t->fd, &ok, 4);
    }
    return EGL_TRUE;
}
EXPORT EGLBoolean eglCopyBuffers(EGLDisplay d, EGLSurface s, NativePixmapType p) { (void)d; (void)s; (void)p; return seterr(EGL_BAD_MATCH); }
EXPORT EGLBoolean eglSwapInterval(EGLDisplay d, EGLint i) { (void)i; return dpy_ok(d) ? EGL_TRUE : seterr(EGL_BAD_DISPLAY); }
EXPORT EGLBoolean eglWaitGL(void) { glFinish(); return EGL_TRUE; }
EXPORT EGLBoolean eglWaitNative(EGLint engine) { (void)engine; return EGL_TRUE; }
EXPORT EGLBoolean eglSurfaceAttrib(EGLDisplay d, EGLSurface s, EGLint a, EGLint v) { (void)s; (void)a; (void)v; return dpy_ok(d) ? EGL_TRUE : seterr(EGL_BAD_DISPLAY); }
EXPORT EGLBoolean eglBindTexImage(EGLDisplay d, EGLSurface s, EGLint b) { (void)d; (void)s; (void)b; return seterr(EGL_BAD_MATCH); }
EXPORT EGLBoolean eglReleaseTexImage(EGLDisplay d, EGLSurface s, EGLint b) { (void)d; (void)s; (void)b; return seterr(EGL_BAD_MATCH); }
EXPORT EGLBoolean eglBindAPI(EGLenum api) { return api == 0x30A0 /* EGL_OPENGL_ES_API */ ? EGL_TRUE : seterr(EGL_BAD_PARAMETER); }
EXPORT EGLenum eglQueryAPI(void) { return 0x30A0; }
EXPORT EGLBoolean eglWaitClient(void) { glFinish(); return EGL_TRUE; }
EXPORT EGLBoolean eglReleaseThread(void) { eglMakeCurrent(DPY, 0, 0, 0); return EGL_TRUE; }
EXPORT void (*eglGetProcAddress(const char *name))(void) {
    extern void *dlsym(void *h, const char *n);
    extern void *dlopen(const char *n, int f);
    static void *self;
    if (!name || name[0] != 'g' || name[1] != 'l') return 0;
    for (int i = 0; i < N1_FUNCS; i++) if (!strcmp(kNames1[i], name)) {
        if (!self) self = dlopen(SELF_SO, 0);
        return self ? (void (*)(void))dlsym(self, name) : 0;
    }
    return 0;
}
