typedef long intptr_t;
extern int __android_log_print(int, const char *, const char *, ...);
/*
 * libGLES_split — переходник перед GL-мостом (libGLES_bridge.so, гостевая часть моста автора).
 *
 * 1. Раздельные имена. Часть загрузчиков EGL (Samsung 4.3, AOSP 4.4+ в раздельном режиме) требуют
 *    libEGL_<тег>.so / libGLESv1_CM_<тег>.so / libGLESv2_<тег>.so. Этот файл кладётся под всеми
 *    именами, а вызовы уходят в единственный экземпляр моста — состояние EGL/GL общее.
 *
 * 2. Загрузка текстур. Мост передаёт на хост w*h*bpp байт, не учитывая GL_UNPACK_ALIGNMENT и
 *    GL_UNPACK_ROW_LENGTH, а драйвер телефона читает с выравниванием строк — при строках некратной
 *    длины (RGB, альфа-текстуры нечётной ширины) он лезет за конец буфера и роняет приложение.
 *    Здесь такие загрузки переупаковываются в плотный буфер с выравниванием 1.
 *
 * Зависит только от libc/libdl, одинаково грузится в bionic Android 2.3–6.0.
 */
#include "names.h"

typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef void GLvoid;

extern void *dlopen(const char *name, int flags);
extern void *dlsym(void *handle, const char *name);
extern void *malloc(unsigned int n);
extern void free(void *p);

#define N (sizeof(kNames) / sizeof(kNames[0]))
static void *g_lib;
static void *g_fn[N];

static void trap(void) { __builtin_trap(); }

static int es1_current(void);
static int zero_fn(void) { return 0; }
static volatile int g_any_es1;   /* в процессе есть контекст ES1 — только тогда проверяем поток */

__attribute__((visibility("hidden"))) void *aemu_split_resolve(unsigned idx) {
    /* функции ES2/ES3 в контексте ES1: у телефона-хозяина их нет в таблице ES1 — был бы вызов по нулю */
    if (g_any_es1 && kEs2Only[idx] && es1_current()) return (void *)zero_fn;
    void *f = g_fn[idx];
    if (f) return f;
    if (!g_lib) g_lib = dlopen("/system/lib/egl/libGLES_bridge.so", 0);
    if (g_lib) f = dlsym(g_lib, kNames[idx]);
    if (!f) f = (void *)trap;
    g_fn[idx] = f;
    return f;
}

#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_PIXEL_UNPACK_BUFFER 0x88EC

/* состояние распаковки текущего потока (гость обычно рисует из одного потока на контекст) */
static int t_align = 4, t_rowlen, t_skiprows, t_skippix, t_pbo;

typedef void (*PixelStoreFn)(GLenum, GLint);
typedef void (*BindBufferFn)(GLenum, unsigned);
typedef void (*TexImageFn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
typedef void (*TexSubImageFn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);

__attribute__((visibility("default"))) void glPixelStorei(GLenum pname, GLint param) {
    switch (pname) {
        case GL_UNPACK_ALIGNMENT: t_align = param; break;
        case GL_UNPACK_ROW_LENGTH: t_rowlen = param; break;
        case GL_UNPACK_SKIP_ROWS: t_skiprows = param; break;
        case GL_UNPACK_SKIP_PIXELS: t_skippix = param; break;
    }
    ((PixelStoreFn)aemu_split_resolve(IDX_glPixelStorei))(pname, param);
}

static void va_bind_note(GLenum target, unsigned buffer);
static void va_context(void *ctx);
static void img_forget_bound(void);
#define GL_TEXTURE_2D_ 0x0DE1
static volatile int g_timg_any;   /* some EGLImage of a GL texture exists: only then the texture calls are looked at */
static void timg_upload(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *px, int whole);
static void tsize_note(GLsizei w, GLsizei h);
static int es1_current(void);

__attribute__((visibility("default"))) void glBindBuffer(GLenum target, unsigned buffer) {
    if (target == GL_PIXEL_UNPACK_BUFFER) t_pbo = buffer != 0;
    va_bind_note(target, buffer);
    ((BindBufferFn)aemu_split_resolve(IDX_glBindBuffer))(target, buffer);
}

/* байт на пиксель; 0 — формат, который не переупаковываем (сжатый, неизвестный) */
static int bytes_per_pixel(GLenum format, GLenum type) {
    int comps;
    switch (format) {
        case 0x1906: /* ALPHA */ case 0x1909: /* LUMINANCE */ case 0x1903: /* RED */
        case 0x1902: /* DEPTH_COMPONENT */ case 0x8D94: /* RED_INTEGER */ comps = 1; break;
        case 0x190A: /* LUMINANCE_ALPHA */ case 0x8227: /* RG */ case 0x8228: /* RG_INTEGER */ comps = 2; break;
        case 0x1907: /* RGB */ case 0x8D98: /* RGB_INTEGER */ comps = 3; break;
        case 0x1908: /* RGBA */ case 0x80E1: /* BGRA */ case 0x8D99: /* RGBA_INTEGER */ comps = 4; break;
        default: return 0;
    }
    switch (type) {
        case 0x1401: /* UNSIGNED_BYTE */ case 0x1400: /* BYTE */ return comps;
        case 0x1403: /* UNSIGNED_SHORT */ case 0x1402: /* SHORT */
        case 0x140B: /* HALF_FLOAT */ case 0x8D61: /* HALF_FLOAT_OES */ return comps * 2;
        case 0x1405: /* UNSIGNED_INT */ case 0x1404: /* INT */ case 0x1406: /* FLOAT */ return comps * 4;
        case 0x8363: /* 5_6_5 */ case 0x8033: /* 4_4_4_4 */ case 0x8034: /* 5_5_5_1 */ return 2;
        case 0x8368: /* 2_10_10_10_REV */ case 0x84FA: /* 24_8 */ return 4;
        default: return 0;
    }
}

/* без memcpy: в bionic 2.x нет __aeabi_memcpy, а зависеть от libgcc не хотим */
static void copy_bytes(unsigned char *d, const unsigned char *s, unsigned long n) {
    if ((((unsigned long)d | (unsigned long)s | n) & 3) == 0) {
        unsigned int *dw = (unsigned int *)d; const unsigned int *sw = (const unsigned int *)s;
        for (unsigned long i = 0; i < n / 4; i++) dw[i] = sw[i];
        return;
    }
    for (unsigned long i = 0; i < n; i++) d[i] = s[i];
}

/*
 * Если данные гостя лежат не плотно (выравнивание строк, длина строки, пропуски) — возвращает
 * плотную копию (освободить free) и выставляет *need = 1; иначе возвращает исходный указатель.
 */
static const void *tighten(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels, int *need) {
    *need = 0;
    if (!pixels || t_pbo || w <= 0 || h <= 0) return pixels;
    int bpp = bytes_per_pixel(format, type);
    if (!bpp) return pixels;
    int align = (t_align == 1 || t_align == 2 || t_align == 4 || t_align == 8) ? t_align : 4;
    unsigned long row = (unsigned long)w * bpp;
    unsigned long srcRowPixels = t_rowlen > 0 ? (unsigned long)t_rowlen : (unsigned long)w;
    unsigned long stride = (srcRowPixels * bpp + align - 1) & ~(unsigned long)(align - 1); /* align — степень двойки */
    if (stride == row && t_skiprows == 0 && t_skippix == 0) {
        /* плотно, но драйвер хоста всё равно выровняет последнюю строку — это безопасно,
           только если выравнивание 1 или строка кратна ему */
        if (align == 1 || (row & (unsigned long)(align - 1)) == 0) return pixels;
    }
    const unsigned char *src = (const unsigned char *)pixels + (unsigned long)t_skiprows * stride + (unsigned long)t_skippix * bpp;
    unsigned char *dst = (unsigned char *)malloc(row * h);
    if (!dst) return pixels;
    for (GLsizei y = 0; y < h; y++) copy_bytes(dst + (unsigned long)y * row, src + (unsigned long)y * stride, row);
    *need = 1;
    return dst;
}

static void unpack_tight(int on) {
    PixelStoreFn ps = (PixelStoreFn)aemu_split_resolve(IDX_glPixelStorei);
    if (on) {
        ps(GL_UNPACK_ALIGNMENT, 1);
        if (t_rowlen) ps(GL_UNPACK_ROW_LENGTH, 0);
        if (t_skiprows) ps(GL_UNPACK_SKIP_ROWS, 0);
        if (t_skippix) ps(GL_UNPACK_SKIP_PIXELS, 0);
    } else {
        ps(GL_UNPACK_ALIGNMENT, t_align);
        if (t_rowlen) ps(GL_UNPACK_ROW_LENGTH, t_rowlen);
        if (t_skiprows) ps(GL_UNPACK_SKIP_ROWS, t_skiprows);
        if (t_skippix) ps(GL_UNPACK_SKIP_PIXELS, t_skippix);
    }
}

__attribute__((visibility("default"))) void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                                                          GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    TexImageFn f = (TexImageFn)aemu_split_resolve(IDX_glTexImage2D);
    img_forget_bound();   /* the texture now has its own storage, no longer an EGLImage alias */
    int need;
    const void *p = tighten(w, h, format, type, pixels, &need);
    if (need) unpack_tight(1);
    f(target, level, ifmt, w, h, border, format, type, p);
    if (need) unpack_tight(0);
    if (target == GL_TEXTURE_2D_ && level == 0 && !es1_current()) tsize_note(w, h);
    if (g_timg_any && target == GL_TEXTURE_2D_ && level == 0) timg_upload(0, 0, w, h, format, type, p, 1);
    if (need) free((void *)p);
}

__attribute__((visibility("default"))) void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                                             GLenum format, GLenum type, const GLvoid *pixels) {
    TexSubImageFn f = (TexSubImageFn)aemu_split_resolve(IDX_glTexSubImage2D);
    int need;
    const void *p = tighten(w, h, format, type, pixels, &need);
    if (need) unpack_tight(1);
    f(target, level, x, y, w, h, format, type, p);
    if (need) unpack_tight(0);
    if (g_timg_any && target == GL_TEXTURE_2D_ && level == 0) timg_upload(x, y, w, h, format, type, p, 0);
    if (need) free((void *)p);
}

/*
 * Контекст ES2 для SurfaceFlinger 4.4. Его RenderEngine создаёт контекст без EGL_CONTEXT_CLIENT_VERSION
 * (то есть ES1) и рисует фиксированным конвейером, а сборки MIUI/MTK при этом ещё и зовут шейдерные
 * функции ES2 (размытие, DRM-значок). На настоящем железе такой вызов в контексте ES1 — ошибка в журнале,
 * а драйвер телефона-хозяина роняет мост нулевым указателем. Если движок выставил AEMU_GL_ES2=1
 * (только для surfaceflinger), просим ES2 — тогда SurfaceFlinger сам выбирает GLES20RenderEngine.
 */
extern char *getenv(const char *name);
typedef void *(*CreateContextFn)(void *dpy, void *config, void *share, const GLint *attribs);
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NONE 0x3038

static void ctx_remember(void *ctx, int ver);
extern long syscall(long n, ...);

static int is_surfaceflinger(void) {
    static int v = -1;
    if (v < 0) {
        char buf[128];
        long fd = syscall(5 /* open */, "/proc/self/cmdline", 0, 0);
        long n = fd >= 0 ? syscall(3 /* read */, fd, buf, sizeof(buf) - 1) : 0;
        if (fd >= 0) syscall(6 /* close */, fd);
        v = 0;
        if (n > 0) {
            buf[n] = 0;
            const char *want = "surfaceflinger";
            for (long i = 0; i + 14 <= n && !v; i++) {
                int k = 0;
                while (k < 14 && buf[i + k] == want[k]) k++;
                if (k == 14) v = 1;
            }
        }
    }
    return v;
}


/* ------------------------------------------------------------ config handles per connection
 * The bridge numbers the configs per connection (each thread has its own): RenderScript's thread got 0x23..
 * where libEGL may have kept another connection's numbering from eglInitialize. Outside the shim a config is
 * always in the first connection's numbering (identical numbering makes this a no-op). */
typedef unsigned (*GetConfigsFn)(void *dpy, void **configs, int size, int *num);
static intptr_t g_cfg_base0;
static intptr_t cfg_base(void *dpy) {
    void *c = 0; int n = 0;
    ((GetConfigsFn)aemu_split_resolve(IDX_eglGetConfigs))(dpy, &c, 1, &n);
    return n > 0 ? (intptr_t)c : 0;
}
static void *cfg_out(void *dpy, void *c, intptr_t cur) {
    if (!g_cfg_base0) g_cfg_base0 = cur;
    return c && cur ? (void *)((intptr_t)c - cur + g_cfg_base0) : c;
}
static void *cfg_in(void *dpy, void *c) {
    if (!c || !g_cfg_base0) return c;
    intptr_t cur = cfg_base(dpy);
    return cur ? (void *)((intptr_t)c - g_cfg_base0 + cur) : c;
}
__attribute__((visibility("default"))) void *eglCreateContext(void *dpy, void *config, void *share, const GLint *attribs) {
    config = cfg_in(dpy, config);
    CreateContextFn f = (CreateContextFn)aemu_split_resolve(IDX_eglCreateContext);
    static int force = -1;
    if (force < 0) { const char *v = getenv("AEMU_GL_ES2"); force = v && v[0] == '1'; }
    int ver = 1;
    if (attribs) for (int i = 0; attribs[i] != EGL_NONE && i < 64; i += 2)
        if (attribs[i] == EGL_CONTEXT_CLIENT_VERSION) ver = attribs[i + 1];
    void *ctx;
    if (force && ver < 2) {
        GLint list[64];
        int n = 0;
        if (attribs) for (int i = 0; attribs[i] != EGL_NONE && n < 60; i += 2) {
            if (attribs[i] == EGL_CONTEXT_CLIENT_VERSION) continue;
            list[n++] = attribs[i]; list[n++] = attribs[i + 1];
        }
        list[n++] = EGL_CONTEXT_CLIENT_VERSION; list[n++] = 2;
        list[n] = EGL_NONE;
        ver = 2;
        ctx = f(dpy, config, share, list);
    } else ctx = f(dpy, config, share, attribs);
    /* гостевая часть моста всем контекстам SurfaceFlinger даёт ES1 (и сообщает «ES-CM 1.1»): учитываем это */
    if (is_surfaceflinger()) ver = 1;
    ctx_remember(ctx, ver);
    return ctx;
}

/* ------------------------------------------------------------ версии контекстов по потокам */

extern long syscall(long n, ...);
#define MAXCTX 256
#define MAXTHR 256
static void *g_ctx[MAXCTX];
static unsigned char g_ctxver[MAXCTX];
static int g_tid[MAXTHR];
static unsigned char g_thrver[MAXTHR];

static void ctx_remember(void *ctx, int ver) {
    if (!ctx) return;
    if (ver < 2) g_any_es1 = 1;
    for (int i = 0; i < MAXCTX; i++) if (g_ctx[i] == ctx || g_ctx[i] == 0) { g_ctx[i] = ctx; g_ctxver[i] = (unsigned char)ver; return; }
}
static int ctx_version(void *ctx) {
    for (int i = 0; i < MAXCTX; i++) if (g_ctx[i] == ctx) return g_ctxver[i];
    return 2;
}
/* the calling thread's id, kept per thread: a gettid system call on every GL call costs more than the call itself
   under the translator (a quarter of the GL thread's time on 7.1) */
extern int pthread_key_create(unsigned *key, void (*destructor)(void *));
extern void *pthread_getspecific(unsigned key);
extern int pthread_setspecific(unsigned key, const void *value);
static unsigned g_tidkey;
static volatile int g_tidkey_state;   /* 0 none, 1 being made, 2 ready */
static int my_tid(void) {
    if (g_tidkey_state != 2) {
        if (__sync_bool_compare_and_swap(&g_tidkey_state, 0, 1)) {
            if (pthread_key_create(&g_tidkey, 0) != 0) { g_tidkey_state = 0; return (int)syscall(224 /* gettid */); }
            g_tidkey_state = 2;
        } else return (int)syscall(224);
    }
    int t = (int)(long)pthread_getspecific(g_tidkey);
    if (t == 0) { t = (int)syscall(224); pthread_setspecific(g_tidkey, (void *)(long)t); }
    return t;
}
static int es1_current(void) {
    int tid = my_tid();
    for (int i = 0; i < MAXTHR; i++) if (g_tid[i] == tid) return g_thrver[i] < 2;
    return 0;
}
static void thread_set(int ver) {
    int tid = my_tid();
    int free = -1;
    for (int i = 0; i < MAXTHR; i++) {
        if (g_tid[i] == tid) { g_thrver[i] = (unsigned char)ver; return; }
        if (g_tid[i] == 0 && free < 0) free = i;
    }
    if (free >= 0) { g_thrver[free] = (unsigned char)ver; g_tid[free] = tid; }
}

/*
 * Samsung's SurfaceFlinger (Galaxy S5, 4.4) sets the viewport and the projection only on its hardware composer path.
 * Without a composer it never does: the context keeps the viewport of the 1x1 start-up pbuffer and an identity
 * projection, the whole screen is composed into one pixel (black screen with a live system behind it). Until
 * SurfaceFlinger calls glViewport itself, binding a surface does what AOSP's DisplayDevice::makeCurrent does.
 */
typedef void (*ViewportFn)(GLint, GLint, GLsizei, GLsizei);
typedef unsigned (*QuerySurfaceFn)(void *dpy, void *surface, GLint attr, GLint *value);
static int g_vp_set;
__attribute__((visibility("default"))) void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    g_vp_set = 1;
    ((ViewportFn)aemu_split_resolve(IDX_glViewport))(x, y, w, h);
}

typedef unsigned (*MakeCurrentFn)(void *dpy, void *draw, void *read, void *ctx);
__attribute__((visibility("default"))) unsigned eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx) {
    unsigned r = ((MakeCurrentFn)aemu_split_resolve(IDX_eglMakeCurrent))(dpy, draw, read, ctx);
    if (r && draw && ctx && !g_vp_set && is_surfaceflinger()) {
        GLint w = 0, h = 0;
        QuerySurfaceFn q = (QuerySurfaceFn)aemu_split_resolve(IDX_eglQuerySurface);
        if (q(dpy, draw, 0x3057 /* EGL_WIDTH */, &w) && q(dpy, draw, 0x3056 /* EGL_HEIGHT */, &h) && w > 0 && h > 0) {
            ((ViewportFn)aemu_split_resolve(IDX_glViewport))(0, 0, w, h);
            ((ViewportFn)aemu_split_resolve(IDX_glScissor))(0, 0, w, h);
            void (*mode)(GLenum) = (void (*)(GLenum))aemu_split_resolve(IDX_glMatrixMode);
            mode(0x1701 /* GL_PROJECTION */);
            ((void (*)(void))aemu_split_resolve(IDX_glLoadIdentity))();
            ((void (*)(float, float, float, float, float, float))aemu_split_resolve(IDX_glOrthof))(0, (float)w, 0, (float)h, 0, 1);
            mode(0x1700 /* GL_MODELVIEW */);
        }
    }
    if (r) { thread_set(ctx ? ctx_version(ctx) : 2); va_context(ctx); }
    return r;
}

/* ------------------------------------------------------------ client-side vertex arrays (ES2)
 *
 * The bridge forwards glVertexAttribPointer with no bound GL_ARRAY_BUFFER as a plain offset: the guest
 * pointer never reaches the host, nothing gets drawn (GB live wallpapers, games → black/flickering
 * frames). Before each draw we copy the enabled client arrays into a scratch VBO, point the attributes
 * at it, draw, then restore the app's state. ES1 contexts are left alone: the bridge handles them.
 */
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define MAXATTR 16
struct va_attr { const void *ptr; GLint size; GLenum type; unsigned char norm, enabled, client; GLsizei stride; };
struct va_state {
    int tid; unsigned array_buf, elem_buf; struct va_attr a[MAXATTR];
    void *ctx;                          /* context the scratch VBO belongs to */
    unsigned sbuf;                      /* scratch VBO for client arrays, kept between draws */
    unsigned char *shadow; unsigned long shadow_len, shadow_cap;   /* bytes last sent into sbuf */
    long host_off[MAXATTR];             /* 1 + offset in sbuf the host attribute points at, 0 = not on sbuf */
    unsigned split_buf;                 /* per-draw buffer of va_upload_split */
};
static struct va_state g_va[64];

static struct va_state *va_cur(void) {
    int tid = my_tid();
    int free = -1;
    for (int i = 0; i < 64; i++) {
        if (g_va[i].tid == tid) return &g_va[i];
        if (g_va[i].tid == 0 && free < 0) free = i;
    }
    if (free < 0) free = tid & 63;   /* table full: reuse a slot (rare, threads are few) */
    struct va_state *v = &g_va[free];
    for (unsigned k = 0; k < sizeof(*v); k++) ((unsigned char *)v)[k] = 0;
    v->tid = tid;
    return v;
}

static void va_bind_note(GLenum target, unsigned buffer) {
    if (target == GL_ARRAY_BUFFER) va_cur()->array_buf = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) va_cur()->elem_buf = buffer;
}

typedef void (*AttribPtrFn)(unsigned, GLint, GLenum, unsigned char, GLsizei, const void *);
typedef void (*AttribArrFn)(unsigned);
typedef void (*DrawArraysFn)(GLenum, GLint, GLsizei);
typedef void (*DrawElementsFn)(GLenum, GLsizei, GLenum, const void *);
typedef void (*GenBuffersFn)(GLsizei, unsigned *);
typedef void (*BufferDataFn)(GLenum, long, const void *, GLenum);
typedef void (*BufferSubDataFn)(GLenum, long, long, const void *);

__attribute__((visibility("default"))) void glVertexAttribPointer(unsigned idx, GLint size, GLenum type, unsigned char norm,
                                                                   GLsizei stride, const void *ptr) {
    struct va_state *v = va_cur();
    if (idx < MAXATTR) {
        struct va_attr *a = &v->a[idx];
        a->ptr = ptr; a->size = size; a->type = type; a->norm = norm; a->stride = stride;
        a->client = v->array_buf == 0 && ptr != 0;
        v->host_off[idx] = 0;
    }
    ((AttribPtrFn)aemu_split_resolve(IDX_glVertexAttribPointer))(idx, size, type, norm, stride, ptr);
}
__attribute__((visibility("default"))) void glEnableVertexAttribArray(unsigned idx) {
    if (idx < MAXATTR) va_cur()->a[idx].enabled = 1;
    ((AttribArrFn)aemu_split_resolve(IDX_glEnableVertexAttribArray))(idx);
}
__attribute__((visibility("default"))) void glDisableVertexAttribArray(unsigned idx) {
    if (idx < MAXATTR) va_cur()->a[idx].enabled = 0;
    ((AttribArrFn)aemu_split_resolve(IDX_glDisableVertexAttribArray))(idx);
}

static int type_size(GLenum t) {
    switch (t) {
        case 0x1400: case 0x1401: return 1;              /* BYTE, UNSIGNED_BYTE */
        case 0x1402: case 0x1403: case 0x140B: case 0x8D61: return 2; /* SHORT, USHORT, HALF_FLOAT(_OES) */
        default: return 4;                               /* FLOAT, FIXED, INT… */
    }
}

static int same_bytes(const unsigned char *a, const unsigned char *b, unsigned long n) {
    if (!a) return 0;
    if (!(((unsigned long)a | (unsigned long)b | n) & 3)) {
        const unsigned *x = (const unsigned *)a, *y = (const unsigned *)b;
        for (unsigned long i = 0; i < n / 4; i++) if (x[i] != y[i]) return 0;
        return 1;
    }
    for (unsigned long i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* the old way, for client arrays far apart in memory: each array copied into a per-draw buffer; returns 2 */
static int va_upload_split(struct va_state *v, long nverts) {
    long total = 0, off[MAXATTR];
    for (int i = 0; i < MAXATTR; i++) {
        struct va_attr *a = &v->a[i];
        off[i] = -1;
        if (!a->enabled || !a->client) continue;
        long esz = (long)a->size * type_size(a->type);
        long stride = a->stride ? a->stride : esz;
        off[i] = total;
        total += ((stride * (nverts - 1) + esz) + 3) & ~3L;
    }
    if (!total) return 0;
    /* one glBufferData with the data: the bridge drops glBufferSubData payloads on large buffers */
    unsigned char *blob = (unsigned char *)malloc((unsigned)total);
    if (!blob) return 0;
    for (int i = 0; i < MAXATTR; i++) {
        if (off[i] < 0) continue;
        struct va_attr *a = &v->a[i];
        long esz = (long)a->size * type_size(a->type);
        long stride = a->stride ? a->stride : esz;
        copy_bytes(blob + off[i], (const unsigned char *)a->ptr, (unsigned long)(stride * (nverts - 1) + esz));
    }
    unsigned buf = 0;
    ((GenBuffersFn)aemu_split_resolve(IDX_glGenBuffers))(1, &buf);
    if (!buf) { free(blob); return 0; }
    BindBufferFn bind = (BindBufferFn)aemu_split_resolve(IDX_glBindBuffer);
    AttribPtrFn ap = (AttribPtrFn)aemu_split_resolve(IDX_glVertexAttribPointer);
    bind(GL_ARRAY_BUFFER, buf);
    ((BufferDataFn)aemu_split_resolve(IDX_glBufferData))(GL_ARRAY_BUFFER, total, blob, GL_STREAM_DRAW);
    free(blob);
    for (int i = 0; i < MAXATTR; i++) {
        if (off[i] < 0) continue;
        struct va_attr *a = &v->a[i];
        ap((unsigned)i, a->size, a->type, a->norm, a->stride, (const void *)off[i]);
    }
    v->split_buf = buf;
    return 2;
}

static void va_restore_split(struct va_state *v) {
    BindBufferFn bind = (BindBufferFn)aemu_split_resolve(IDX_glBindBuffer);
    AttribPtrFn ap = (AttribPtrFn)aemu_split_resolve(IDX_glVertexAttribPointer);
    bind(GL_ARRAY_BUFFER, 0);
    for (int i = 0; i < MAXATTR; i++) {
        struct va_attr *a = &v->a[i];
        if (a->enabled && a->client) { ap((unsigned)i, a->size, a->type, a->norm, a->stride, a->ptr); v->host_off[i] = 0; }
    }
    bind(GL_ARRAY_BUFFER, v->array_buf);
    ((GenBuffersFn)aemu_split_resolve(IDX_glDeleteBuffers))(1, &v->split_buf);  /* same signature as glGenBuffers */
    v->split_buf = 0;
}

/* a thread's scratch VBO belongs to the context current when it was made */
static void va_context(void *ctx) {
    struct va_state *v = va_cur();
    if (v->ctx == ctx) return;
    v->ctx = ctx; v->sbuf = 0; v->shadow_len = 0;
    for (int i = 0; i < MAXATTR; i++) v->host_off[i] = 0;
}

/* Uploads client arrays for vertices [0, nverts) into the thread's scratch VBO and points the attributes
 * at it; returns 1 if it did. Interleaved arrays share one span of client memory, so the span is sent once;
 * the VBO stays alive between draws and is re-filled only when the bytes differ from the last upload, and
 * attribute pointers are re-sent only when they moved (per-draw GenBuffers/BufferData/DeleteBuffers and
 * 2x pointers made every client-array draw a dozen bridge calls). */
static int va_upload_f(struct va_state *v, long nverts, int force) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("AEMU_GL_CLIENT_ARRAYS"); on = !(e && e[0] == '0'); }
    if (!(on || force) || nverts <= 0 || es1_current()) return 0;
    unsigned long lo = ~0UL, hi = 0, sum = 0;
    for (int i = 0; i < MAXATTR; i++) {
        struct va_attr *a = &v->a[i];
        if (!a->enabled || !a->client) continue;
        long esz = (long)a->size * type_size(a->type);
        long stride = a->stride ? a->stride : esz;
        unsigned long p = (unsigned long)a->ptr, n = (unsigned long)(stride * (nverts - 1) + esz);
        if (p < lo) lo = p;
        if (p + n > hi) hi = p + n;
        sum += n;
    }
    if (!sum) return 0;
    unsigned long len = hi - lo;
    if (len > sum * 4 + 4096) return va_upload_split(v, nverts);   /* far-apart arrays: copy each */
    BindBufferFn bind = (BindBufferFn)aemu_split_resolve(IDX_glBindBuffer);
    AttribPtrFn ap = (AttribPtrFn)aemu_split_resolve(IDX_glVertexAttribPointer);
    if (!v->sbuf) {
        ((GenBuffersFn)aemu_split_resolve(IDX_glGenBuffers))(1, &v->sbuf);
        if (!v->sbuf) return 0;
        v->shadow_len = 0;
    }
    bind(GL_ARRAY_BUFFER, v->sbuf);
    if (v->shadow_len != len || !same_bytes(v->shadow, (const unsigned char *)lo, len)) {
        ((BufferDataFn)aemu_split_resolve(IDX_glBufferData))(GL_ARRAY_BUFFER, (long)len, (const void *)lo, GL_STREAM_DRAW);
        if (v->shadow_cap < len) {
            free(v->shadow);
            v->shadow_cap = len < 4096 ? 4096 : len;
            v->shadow = (unsigned char *)malloc((unsigned)v->shadow_cap);
            if (!v->shadow) v->shadow_cap = 0;
        }
        if (v->shadow) { copy_bytes(v->shadow, (const unsigned char *)lo, len); v->shadow_len = len; }
        else v->shadow_len = 0;
    }
    for (int i = 0; i < MAXATTR; i++) {
        struct va_attr *a = &v->a[i];
        if (!a->enabled || !a->client) continue;
        long off = (long)((unsigned long)a->ptr - lo);
        if (v->host_off[i] != off + 1) {
            ap((unsigned)i, a->size, a->type, a->norm, a->stride, (const void *)off);
            v->host_off[i] = off + 1;
        }
    }
    return 1;
}

static int va_upload(struct va_state *v, long nverts) { return va_upload_f(v, nverts, 0); }

/* after the draw only the app's GL_ARRAY_BUFFER binding is put back; the attributes stay on the scratch
 * VBO (they are client arrays, so the next draw goes through va_upload again) */
static void va_restore(struct va_state *v, int used) {
    if (used == 2) { va_restore_split(v); return; }
    ((BindBufferFn)aemu_split_resolve(IDX_glBindBuffer))(GL_ARRAY_BUFFER, v->array_buf);
}

__attribute__((visibility("default"))) void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    struct va_state *v = va_cur();
    int buf = count > 0 ? va_upload(v, (long)first + count) : 0;
    ((DrawArraysFn)aemu_split_resolve(IDX_glDrawArrays))(mode, first, count);
    if (buf) va_restore(v, buf);
}

/* Copies of element-array buffer contents. With indices in a bound buffer and some attributes still client
 * arrays (2.3 RenderScript, Launcher2 3D all-apps: a stale stack pointer left enabled), the bridge cannot know
 * the vertex count and copies from the client pointer until it runs off the stack (SIGSEGV in callv_x, the
 * launcher restarted forever). The max index is read from this copy, and the client arrays are uploaded. */
#define MAXEBO 128
static struct { unsigned id; unsigned long len; unsigned char *data; } g_ebo[MAXEBO];
static unsigned bound_id(GLenum target);

static int ebo_slot(unsigned id, int make) {
    int fr = -1;
    for (int i = 0; i < MAXEBO; i++) {
        if (g_ebo[i].id == id) return i;
        if (!g_ebo[i].id && fr < 0) fr = i;
    }
    if (!make) return -1;
    if (fr < 0) { fr = (int)(id % MAXEBO); free(g_ebo[fr].data); g_ebo[fr].data = 0; g_ebo[fr].len = 0; }
    g_ebo[fr].id = id;
    return fr;
}

/* id of the buffer bound to target; the data of index buffers can arrive through GL_ARRAY_BUFFER (2.3 RS
 * fills it there, then binds the same buffer as GL_ELEMENT_ARRAY_BUFFER), so small buffers of both are copied */
#define EBO_COPY_MAX 65536
static unsigned bound_id(GLenum target) {
    struct va_state *v = va_cur();
    return target == GL_ELEMENT_ARRAY_BUFFER ? v->elem_buf : target == GL_ARRAY_BUFFER ? v->array_buf : 0;
}

__attribute__((visibility("default"))) void glBufferData(GLenum target, long size, const void *data, GLenum usage) {
    unsigned id = bound_id(target);
    if (id && size > EBO_COPY_MAX) { int s = ebo_slot(id, 0); if (s >= 0) { free(g_ebo[s].data); g_ebo[s].data = 0; g_ebo[s].len = 0; g_ebo[s].id = 0; } id = 0; }
    if (id) {
        int s = ebo_slot(id, 1);
        free(g_ebo[s].data); g_ebo[s].data = 0; g_ebo[s].len = 0;
        if (size > 0 && (g_ebo[s].data = (unsigned char *)malloc((unsigned)size))) {
            g_ebo[s].len = (unsigned long)size;
            if (data) copy_bytes(g_ebo[s].data, (const unsigned char *)data, (unsigned long)size);
            else for (long i = 0; i < size; i++) g_ebo[s].data[i] = 0;
        }
    }
    ((BufferDataFn)aemu_split_resolve(IDX_glBufferData))(target, size, data, usage);
}

__attribute__((visibility("default"))) void glBufferSubData(GLenum target, long off, long size, const void *data) {
    unsigned id = bound_id(target);
    int s = id ? ebo_slot(id, 0) : -1;
    if (s >= 0 && data && off >= 0 && size > 0 && (unsigned long)(off + size) <= g_ebo[s].len)
        copy_bytes(g_ebo[s].data + off, (const unsigned char *)data, (unsigned long)size);
    ((BufferSubDataFn)aemu_split_resolve(IDX_glBufferSubData))(target, off, size, data);
}

__attribute__((visibility("default"))) void glDeleteBuffers(GLsizei n, const unsigned *ids) {
    for (GLsizei i = 0; ids && i < n; i++) {
        int s = ids[i] ? ebo_slot(ids[i], 0) : -1;
        if (s >= 0) { free(g_ebo[s].data); g_ebo[s].data = 0; g_ebo[s].len = 0; g_ebo[s].id = 0; }
    }
    ((GenBuffersFn)aemu_split_resolve(IDX_glDeleteBuffers))(n, (unsigned *)ids);
}

static long max_index(const void *p, GLsizei count, GLenum type) {
    long maxi = 0;
    for (GLsizei i = 0; i < count; i++) {
        long x = type == 0x1401 ? ((const unsigned char *)p)[i]
               : type == 0x1403 ? ((const unsigned short *)p)[i]
               : (long)((const unsigned int *)p)[i];
        if (x > maxi) maxi = x;
    }
    return maxi;
}

static int any_client(struct va_state *v) {
    for (int i = 0; i < MAXATTR; i++) if (v->a[i].enabled && v->a[i].client) return 1;
    return 0;
}

__attribute__((visibility("default"))) void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    struct va_state *v = va_cur();
    int buf = 0;
    if (count > 0 && v->elem_buf == 0 && indices) {
        buf = va_upload(v, max_index(indices, count, type) + 1);
    } else if (count > 0 && v->elem_buf && any_client(v)) {
        int s = ebo_slot(v->elem_buf, 0);
        unsigned long off = (unsigned long)indices, need = (unsigned long)count * type_size(type);
        if (s >= 0 && g_ebo[s].data && off + need <= g_ebo[s].len)
            buf = va_upload_f(v, max_index(g_ebo[s].data + off, count, type) + 1, 1);   /* the bridge cannot do this case */
        static int warned;
        if (!buf && warned++ < 3)
            __android_log_print(5, "aemu-split", "DrawElements ebo %u slot %d len %lu off %lu need %lu es1 %d maxi %ld: client arrays not uploaded",
                                v->elem_buf, s, s >= 0 ? g_ebo[s].len : 0, off, need, es1_current(),
                                s >= 0 && g_ebo[s].data ? max_index(g_ebo[s].data + off, count, type) : -1L);
    }
    ((DrawElementsFn)aemu_split_resolve(IDX_glDrawElements))(mode, count, type, indices);
    if (buf) va_restore(v, buf);
}

/* ------------------------------------------------------------ EGLImage textures (SurfaceFlinger layers)
 *
 * On real hardware an EGLImage texture aliases the gralloc buffer, so every new frame the app queues is
 * visible as soon as SurfaceFlinger binds the texture. The bridge instead copies the buffer's pixels once,
 * inside glEGLImageTargetTexture2DOES. GB/ICS SurfaceFlinger calls that only the first time it sees a
 * buffer and later just binds the texture — the layer freezes on its first contents (still animations)
 * or on an empty buffer (black flicker with double buffering). Re-target the image on bind, at most once
 * per SurfaceFlinger frame, so the bridge re-reads the buffer.
 */
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#define MAXIMG 256
struct img_tex { unsigned tex; GLenum target; void *image; unsigned frame; };
static struct img_tex g_img[MAXIMG];
static unsigned g_frame = 1, g_bound2d, g_boundext;

typedef void (*BindTexFn)(GLenum, unsigned);
typedef void (*ImageTargetFn)(GLenum, void *);
typedef void (*DeleteTexFn)(GLsizei, const unsigned *);
typedef unsigned (*SwapFn)(void *, void *);

static struct img_tex *img_find(unsigned tex) {
    if (!tex) return 0;
    for (int i = 0; i < MAXIMG; i++) if (g_img[i].tex == tex) return &g_img[i];
    return 0;
}
static void img_forget_bound(void) {
    struct img_tex *e = img_find(g_bound2d);
    if (e && e->target == GL_TEXTURE_2D) e->tex = 0;
}

/* ------------------------------------------------------------ EGLImage of a GL texture (EGL_KHR_gl_texture_2D_image)
 *
 * 3.x libEGL lists EGL_KHR_gl_texture_2D_image in its fixed extension string, but the bridge makes images of
 * window buffers only: eglCreateImageKHR(EGL_GL_TEXTURE_2D_KHR) gave EGL_NO_IMAGE_KHR. 3.x WebKit paints page
 * tiles on its own thread and context and hands them to the UI context through such images ("GLUtils: EGL ERROR -
 * eglCreateImage() returned 0"): every page stayed white. Here the image is the source texture's pixels, kept on
 * the guest side. Uploads into the source (glTexImage2D/glTexSubImage2D, level 0) update it and bump its version;
 * a texture of another context targeted at the image (glEGLImageTargetTexture2DOES) gets the pixels then, and again
 * whenever it is bound after a newer version: what shared storage looks like to the image's user. Pixels the
 * texture already had when the image was made are read back through a framebuffer (ES2 and later contexts).
 */
#define MAXTIMG 128
#define MAXTLINK 256
struct timg { int used; void *ctx; unsigned tex; GLsizei w, h; GLenum fmt, type; int bpp; unsigned char *px; unsigned ver; };
struct tlink { void *ctx; unsigned tex; struct timg *img; unsigned ver; };
static struct timg g_timg[MAXTIMG];
static struct tlink g_tlink[MAXTLINK];
static volatile int g_tlock;
static void tl(void) { while (__sync_lock_test_and_set(&g_tlock, 1)) syscall(158 /* sched_yield */); }
static void tu(void) { __sync_lock_release(&g_tlock); }
typedef void *(*CurCtxFn)(void);
static void *cur_ctx(void) { return ((CurCtxFn)aemu_split_resolve(IDX_eglGetCurrentContext))(); }
/* the 2D texture bound on this thread: g_bound2d is one for the process, and WebKit's tile thread uploads while the
   UI thread binds others (pthread key: old bionic has no __thread) */
static unsigned g_bkey; static volatile int g_bkey_state;
static int bkey(void) {
    if (g_bkey_state == 2) return 1;
    if (__sync_bool_compare_and_swap(&g_bkey_state, 0, 1)) {
        if (pthread_key_create(&g_bkey, 0) != 0) { g_bkey_state = 0; return 0; }
        g_bkey_state = 2; return 1;
    }
    return g_bkey_state == 2;
}
static void t_bound_set(unsigned tex) { if (bkey()) pthread_setspecific(g_bkey, (void *)(unsigned long)tex); }
static unsigned t_bound(void) { return bkey() ? (unsigned)(unsigned long)pthread_getspecific(g_bkey) : g_bound2d; }
static struct timg *timg_of(void *image) {
    struct timg *t = (struct timg *)image;
    return t >= g_timg && t < g_timg + MAXTIMG && t->used ? t : 0;
}
/* the image whose source is texture tex of context ctx */
static struct timg *timg_source(void *ctx, unsigned tex) {
    for (int i = 0; i < MAXTIMG; i++) if (g_timg[i].used && g_timg[i].ctx == ctx && g_timg[i].tex == tex) return &g_timg[i];
    return 0;
}
static void timg_store(struct timg *t, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *px, int whole) {
    int bpp = bytes_per_pixel(format, type);
    if (!bpp || !px) {
        if (whole) { free(t->px); t->px = 0; t->w = w; t->h = h; t->fmt = format; t->type = type; t->bpp = bpp; t->ver++; }
        return;
    }
    if (whole || !t->px || t->fmt != format || t->type != type) {
        if (!whole && (x != 0 || y != 0 || w != t->w || h != t->h)) return;   /* a part in another format: nothing to merge into */
        unsigned char *n = (unsigned char *)malloc((unsigned long)w * h * bpp);
        if (!n) return;
        free(t->px); t->px = n; t->w = w; t->h = h; t->fmt = format; t->type = type; t->bpp = bpp;
    }
    if (x < 0 || y < 0 || x + w > t->w || y + h > t->h) return;
    unsigned long row = (unsigned long)w * bpp, stride = (unsigned long)t->w * bpp;
    for (GLsizei r = 0; r < h; r++)
        copy_bytes(t->px + (unsigned long)(y + r) * stride + (unsigned long)x * bpp, (const unsigned char *)px + (unsigned long)r * row, row);
    t->ver++;
}
static void timg_upload(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *px, int whole) {
    if (t_pbo) return;
    void *ctx = cur_ctx();
    tl();
    struct timg *t = timg_source(ctx, t_bound());
    if (t) timg_store(t, x, y, w, h, format, type, px, whole);
    else if (whole) for (int i = 0; i < MAXTLINK; i++)   /* a target given storage of its own is no longer the image's */
        if (g_tlink[i].img && g_tlink[i].ctx == ctx && g_tlink[i].tex == t_bound()) g_tlink[i].img = 0;
    tu();
}
/* the image's pixels into the texture bound now (the caller holds the lock) */
static void timg_put(struct timg *t, GLenum target) {
    if (!t->px) return;
    unpack_tight(1);
    ((TexImageFn)aemu_split_resolve(IDX_glTexImage2D))(target, 0, (GLint)t->fmt, t->w, t->h, 0, t->fmt, t->type, t->px);
    unpack_tight(0);
}
static void timg_bound(unsigned tex) {
    if (!tex) return;
    void *ctx = cur_ctx();
    tl();
    for (int i = 0; i < MAXTLINK; i++) {
        struct tlink *l = &g_tlink[i];
        if (l->img && l->ctx == ctx && l->tex == tex) {
            if (l->img->used && l->ver != l->img->ver) { timg_put(l->img, GL_TEXTURE_2D_); l->ver = l->img->ver; }
            break;
        }
    }
    tu();
}
static void timg_target(GLenum target, void *image) {
    void *ctx = cur_ctx();
    unsigned tex = t_bound();
    tl();
    struct timg *t = timg_of(image);
    struct tlink *l = 0;
    for (int i = 0; i < MAXTLINK && !l; i++) if (g_tlink[i].img && g_tlink[i].ctx == ctx && g_tlink[i].tex == tex) l = &g_tlink[i];
    for (int i = 0; i < MAXTLINK && !l; i++) if (!g_tlink[i].img) l = &g_tlink[i];
    if (t && l && tex) { l->ctx = ctx; l->tex = tex; l->img = t; l->ver = t->ver; timg_put(t, target); }
    tu();
}
static void timg_deleted(GLsizei n, const unsigned *tex) {
    void *ctx = cur_ctx();
    tl();
    for (GLsizei k = 0; tex && k < n; k++)
        for (int i = 0; i < MAXTLINK; i++) if (g_tlink[i].img && g_tlink[i].ctx == ctx && g_tlink[i].tex == tex[k]) g_tlink[i].img = 0;
    tu();
}
/* pixels the texture already has: read back through a framebuffer */
typedef void (*GetIntFn)(GLenum, GLint *);
typedef void (*GenFbFn)(GLsizei, unsigned *);
typedef void (*BindFbFn)(GLenum, unsigned);
typedef void (*FbTexFn)(GLenum, GLenum, GLenum, unsigned, GLint);
typedef GLenum (*FbStatusFn)(GLenum);
typedef void (*ReadPixFn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (*DelFbFn)(GLsizei, const unsigned *);
static void timg_readback(struct timg *t) {
    if (es1_current() || t->w <= 0 || t->h <= 0) return;
    unsigned char *px = (unsigned char *)malloc((unsigned long)t->w * t->h * 4);
    if (!px) return;
    GLint old = 0; unsigned fb = 0;
    ((GetIntFn)aemu_split_resolve(IDX_glGetIntegerv))(0x8CA6 /* FRAMEBUFFER_BINDING */, &old);
    ((GenFbFn)aemu_split_resolve(IDX_glGenFramebuffers))(1, &fb);
    ((BindFbFn)aemu_split_resolve(IDX_glBindFramebuffer))(0x8D40 /* FRAMEBUFFER */, fb);
    ((FbTexFn)aemu_split_resolve(IDX_glFramebufferTexture2D))(0x8D40, 0x8CE0 /* COLOR_ATTACHMENT0 */, GL_TEXTURE_2D_, t->tex, 0);
    int ok = ((FbStatusFn)aemu_split_resolve(IDX_glCheckFramebufferStatus))(0x8D40) == 0x8CD5 /* COMPLETE */;
    PixelStoreFn ps = (PixelStoreFn)aemu_split_resolve(IDX_glPixelStorei);
    if (ok) {
        ps(0x0D05 /* PACK_ALIGNMENT */, 1);
        ((ReadPixFn)aemu_split_resolve(IDX_glReadPixels))(0, 0, t->w, t->h, 0x1908 /* RGBA */, 0x1401 /* UNSIGNED_BYTE */, px);
        ps(0x0D05, 4);
    }
    ((BindFbFn)aemu_split_resolve(IDX_glBindFramebuffer))(0x8D40, (unsigned)old);
    ((DelFbFn)aemu_split_resolve(IDX_glDeleteFramebuffers))(1, &fb);
    if (!ok) { free(px); return; }
    free(t->px); t->px = px; t->fmt = 0x1908; t->type = 0x1401; t->bpp = 4; t->ver++;
}
/* sizes of level-0 2D textures per context, for the read-back */
struct tsize { void *ctx; unsigned tex; GLsizei w, h; };
static struct tsize g_tsize[512];
static void tsize_note(GLsizei w, GLsizei h) {
    void *ctx = cur_ctx(); unsigned tex = t_bound(); int fr = -1;
    for (int i = 0; i < 512; i++) {
        if (g_tsize[i].ctx == ctx && g_tsize[i].tex == tex) { g_tsize[i].w = w; g_tsize[i].h = h; return; }
        if (!g_tsize[i].ctx && fr < 0) fr = i;
    }
    if (fr < 0) fr = (int)(tex & 511);
    g_tsize[fr].ctx = ctx; g_tsize[fr].tex = tex; g_tsize[fr].w = w; g_tsize[fr].h = h;
}
static void *timg_create(void *ctx, unsigned tex, const GLint *attribs) {
    GLint level = 0;
    for (int i = 0; attribs && attribs[i] != EGL_NONE && i < 32; i += 2) if (attribs[i] == 0x30BC /* GL_TEXTURE_LEVEL */) level = attribs[i + 1];
    if (!tex || level != 0) return 0;
    void *cur = cur_ctx();
    if (!ctx) ctx = cur;
    tl();
    struct timg *t = timg_source(ctx, tex);
    for (int i = 0; i < MAXTIMG && !t; i++) if (!g_timg[i].used) t = &g_timg[i];
    if (t) {
        if (!t->used) { t->ctx = ctx; t->tex = tex; t->w = t->h = 0; t->px = 0; t->ver = 1; t->used = 1; }
        for (int i = 0; i < 512; i++) if (g_tsize[i].ctx == ctx && g_tsize[i].tex == tex) { t->w = g_tsize[i].w; t->h = g_tsize[i].h; }
        if (!t->px && ctx == cur) timg_readback(t);
        g_timg_any = 1;
    }
    tu();
    return t;
}
static void timg_destroy(struct timg *t) {
    tl();
    for (int i = 0; i < MAXTLINK; i++) if (g_tlink[i].img == t) g_tlink[i].img = 0;
    free(t->px); t->px = 0; t->used = 0;
    tu();
}

__attribute__((visibility("default"))) void glBindTexture(GLenum target, unsigned tex) {
    ((BindTexFn)aemu_split_resolve(IDX_glBindTexture))(target, tex);
    if (target == GL_TEXTURE_2D) { g_bound2d = tex; t_bound_set(tex); if (g_timg_any) timg_bound(tex); }
    else if (target == GL_TEXTURE_EXTERNAL_OES) g_boundext = tex;
    else return;
    static int on = -1;
    if (on < 0) { const char *v = getenv("AEMU_GL_RETARGET"); on = !(v && v[0] == '0'); }
    struct img_tex *e = on ? img_find(tex) : 0;
    if (e && e->target == target && e->frame != g_frame) {
        e->frame = g_frame;
        ((ImageTargetFn)aemu_split_resolve(IDX_glEGLImageTargetTexture2DOES))(target, e->image);
    }
}

__attribute__((visibility("default"))) void glEGLImageTargetTexture2DOES(GLenum target, void *image) {
    if (timg_of(image)) { timg_target(target, image); return; }
    ((ImageTargetFn)aemu_split_resolve(IDX_glEGLImageTargetTexture2DOES))(target, image);
    unsigned tex = target == GL_TEXTURE_EXTERNAL_OES ? g_boundext : g_bound2d;
    if (!tex) return;
    struct img_tex *e = img_find(tex);
    if (!e) for (int i = 0; i < MAXIMG && !e; i++) if (!g_img[i].tex) e = &g_img[i];
    if (!e) e = &g_img[tex % MAXIMG];
    e->tex = tex; e->target = target; e->image = image; e->frame = g_frame;
}

__attribute__((visibility("default"))) void glDeleteTextures(GLsizei n, const unsigned *tex) {
    for (GLsizei i = 0; tex && i < n; i++) { struct img_tex *e = img_find(tex[i]); if (e) e->tex = 0; }
    if (g_timg_any) timg_deleted(n, tex);
    ((DeleteTexFn)aemu_split_resolve(IDX_glDeleteTextures))(n, tex);
}

__attribute__((visibility("default"))) unsigned eglSwapBuffers(void *dpy, void *surface) {
    g_frame++;
    return ((SwapFn)aemu_split_resolve(IDX_eglSwapBuffers))(dpy, surface);
}

/*
 * 7.0+ libEGL takes eglSwapBuffersWithDamageKHR / eglSetDamageRegionKHR from the driver when it offers them, and
 * hwui then presents only through them. The bridge hands out empty stubs for both, so no app frame ever reached
 * SurfaceFlinger (black screen): damage is ignored and the whole surface is swapped.
 */
static unsigned swap_with_damage(void *dpy, void *surface, const GLint *rects, GLint n) {
    (void)rects; (void)n;
    return eglSwapBuffers(dpy, surface);
}
static unsigned set_damage_region(void *dpy, void *surface, const GLint *rects, GLint n) {
    (void)dpy; (void)surface; (void)rects; (void)n;
    return 1;
}
/*
 * 7.0+ app window buffers carry SW_READ/WRITE_OFTEN usage (0x933); the bridge refuses EGLImages for them, so
 * SurfaceFlinger never latched launcher or status bar frames and the apps stalled on dequeue. On failure the
 * image is retried with the software bits hidden (ANativeWindowBuffer.usage, after the 32-byte base header).
 */
/* 7.x SurfaceFlinger runs with stdin closed, so a gralloc buffer can land on descriptor 0, which the bridge
 * takes for "no fd" (layer shown empty, the app stalls): keep 0..2 occupied */
__attribute__((constructor)) static void hold_std_fds(void) {
    for (int fd = 0; fd < 3; fd++)
        if (syscall(55 /* fcntl */, fd, 1 /* F_GETFD */) < 0) syscall(5 /* open */, "/dev/null", 2 /* O_RDWR */, 0);
}

typedef void *(*CreateImageFn)(void *dpy, void *ctx, unsigned target, void *buf, const GLint *attribs);
__attribute__((visibility("default"))) void *eglCreateImageKHR(void *dpy, void *ctx, unsigned target, void *buf, const GLint *attribs) {
    CreateImageFn f = (CreateImageFn)aemu_split_resolve(IDX_eglCreateImageKHR);
    if (target == 0x30B1 /* EGL_GL_TEXTURE_2D_KHR */) return timg_create(ctx, (unsigned)(unsigned long)buf, attribs);
    /* (the descriptors may be taken before this library is loaded: a buffer already sitting on 0 moves up, its
     * handle follows it, and /dev/null takes 0 so later buffers never land there) */
    if (target == 0x3140 && buf) {
        int *h = *(int **)((char *)buf + 60);           /* ANativeWindowBuffer.handle: version, numFds, numInts, data */
        if (h && h[1] > 0 && h[3] == 0) {
            int nfd = syscall(55 /* fcntl */, 0, 0 /* F_DUPFD */, 3);
            if (nfd >= 3) {
                h[3] = nfd;
                syscall(6 /* close */, 0);
                syscall(5 /* open */, "/dev/null", 2 /* O_RDWR */, 0);
            }
        }
    }
    void *img = f(dpy, ctx, target, buf, attribs);
    if (img || target != 0x3140 /* EGL_NATIVE_BUFFER_ANDROID */ || !buf) return img;
    int *usage = (int *)((char *)buf + 48);
    int old = *usage;
    if (old & 0xff) {
        *usage = old & ~0xff;
        img = f(dpy, ctx, target, buf, attribs);
        *usage = old;
        if (img) return img;
    }
    /* RGBX_8888 buffers (a GLSurfaceView's default 7.x window) are refused too: the memory is laid out as RGBA_8888
     * and SurfaceFlinger draws an RGBX layer opaque anyway, so the image is made as RGBA (ANativeWindowBuffer.format) */
    int *format = (int *)((char *)buf + 44);
    if (*format != 2 /* HAL_PIXEL_FORMAT_RGBX_8888 */) return img;
    *format = 1;
    *usage = old & ~0xff;
    img = f(dpy, ctx, target, buf, attribs);
    *usage = old;
    *format = 2;
    return img;
}

/* EGL_SYNC_NATIVE_FENCE_ANDROID: the bridge does not list EGL_ANDROID_native_fence_sync, yet hands out a sync and
 * then an fd that is no sync file. SurfaceFlinger (captureScreenImplLocked, 4.4+) asks for this type without
 * checking the extension string and queues the screenshot with that fd; the consumer's sync_wait fails
 * ("GLConsumer: doGLFenceWait: error waiting for fence: -25/-13") and SurfaceTexture.updateTexImage throws - on 7.1
 * the screen-off animation (ColorFade) took system_server down at every power-key press. EGL_KHR_fence_sync: a type
 * the implementation does not support gives EGL_NO_SYNC_KHR; SurfaceFlinger then falls back to a plain fence sync. */
typedef void *(*CreateSyncFn)(void *dpy, unsigned type, const GLint *attribs);
__attribute__((visibility("default"))) void *eglCreateSyncKHR(void *dpy, unsigned type, const GLint *attribs) {
    if (type == 0x3144 /* EGL_SYNC_NATIVE_FENCE_ANDROID */) return 0;
    return ((CreateSyncFn)aemu_split_resolve(IDX_eglCreateSyncKHR))(dpy, type, attribs);
}

typedef void *(*ProcFn)(const char *);
typedef unsigned (*DestroyImageFn)(void *, void *);
__attribute__((visibility("default"))) unsigned eglDestroyImageKHR(void *dpy, void *image) {
    struct timg *t = timg_of(image);
    if (t) { timg_destroy(t); return 1; }
    return ((DestroyImageFn)aemu_split_resolve(IDX_eglDestroyImageKHR))(dpy, image);
}

__attribute__((visibility("default"))) void *eglGetProcAddress(const char *name) {
    if (name && !__builtin_strcmp(name, "eglCreateSyncKHR")) return (void *)eglCreateSyncKHR;
    /* the bridge's own entries would skip the texture images */
    if (name && !__builtin_strcmp(name, "eglCreateImageKHR")) return (void *)eglCreateImageKHR;
    if (name && !__builtin_strcmp(name, "eglDestroyImageKHR")) return (void *)eglDestroyImageKHR;
    if (name && !__builtin_strcmp(name, "glEGLImageTargetTexture2DOES")) return (void *)glEGLImageTargetTexture2DOES;
    static const char *const names[] = { "eglSwapBuffersWithDamageKHR", "eglSwapBuffersWithDamageEXT", "eglSetDamageRegionKHR" };
    static void *const fns[] = { (void *)swap_with_damage, (void *)swap_with_damage, (void *)set_damage_region };
    for (int i = 0; name && i < 3; i++) {
        int k = 0;
        while (names[i][k] && name[k] == names[i][k]) k++;
        if (!names[i][k] && !name[k]) return fns[i];
    }
    return ((ProcFn)aemu_split_resolve(IDX_eglGetProcAddress))(name);
}

/* ------------------------------------------------------------ extra SurfaceFlinger windows (4.3+)
 *
 * The bridge treats every window surface SurfaceFlinger creates as "the screen" and moves the phone's
 * display onto it. 4.3 SurfaceFlinger also creates window surfaces for virtual displays / screenshots
 * (pressing Home in TouchWiz does it): the bridge switches to that 1x1 window, fails to attach it and never
 * switches back — the emulator shows an empty screen. In surfaceflinger only the first window (the primary
 * display) gets a real window surface; any other window gets an off-screen pbuffer of the same size.
 */
typedef void *(*CreateWinFn)(void *dpy, void *config, void *win, const GLint *attribs);
typedef void *(*CreatePbufFn)(void *dpy, void *config, const GLint *attribs);
typedef int (*WinQueryFn)(const void *win, int what, int *value);
static void *g_primary_win;
typedef unsigned (*GetAttrFn)(void *dpy, void *config, int attr, int *value);
typedef unsigned (*ChooseFn)(void *dpy, const GLint *attribs, void **configs, int size, int *num);

__attribute__((visibility("default"))) void *eglCreateWindowSurface(void *dpy, void *config, void *win, const GLint *attribs) {
    config = cfg_in(dpy, config);
    CreateWinFn f = (CreateWinFn)aemu_split_resolve(IDX_eglCreateWindowSurface);
    if (!is_surfaceflinger() || !win) {
        void *r = f(dpy, config, win, attribs);
        if (r || !win) return r;
        /* The window's pixel format does not match the config (RenderScript: RGBA_8888 window, RGB565
         * config — GB's 3D all-apps list stayed blank): retry with a config of the window's format. */
        int fmt = 0, depth = 0, stencil = 0, n = 0;
        WinQueryFn q = *(WinQueryFn *)((char *)win + 84);
        if (q) q(win, 2 /* NATIVE_WINDOW_FORMAT */, &fmt);
        GetAttrFn ga = (GetAttrFn)aemu_split_resolve(IDX_eglGetConfigAttrib);
        ga(dpy, config, 0x3025 /* EGL_DEPTH_SIZE */, &depth);
        ga(dpy, config, 0x3026 /* EGL_STENCIL_SIZE */, &stencil);
        int rgb565 = fmt == 4, alpha = fmt == 1 ? 8 : 0;
        GLint ca[] = { 0x3024, rgb565 ? 5 : 8, 0x3023, rgb565 ? 6 : 8, 0x3022, rgb565 ? 5 : 8, 0x3021, alpha,
                       0x3025, depth, 0x3026, stencil, 0x3033 /* SURFACE_TYPE */, 4 /* WINDOW */,
                       0x3040 /* RENDERABLE_TYPE */, 4 /* ES2 */, EGL_NONE };
        void *cfg = 0;
        if (((ChooseFn)aemu_split_resolve(IDX_eglChooseConfig))(dpy, ca, &cfg, 1, &n) && n > 0 && cfg != config)
            r = f(dpy, cfg, win, attribs);
        return r;
    }
    if (!g_primary_win) g_primary_win = win;
    if (win == g_primary_win) return f(dpy, config, win, attribs);
    int w = 1, h = 1;
    WinQueryFn q = *(WinQueryFn *)((char *)win + 84);   /* ANativeWindow::query (32-bit layout) */
    if (q) { q(win, 0 /* NATIVE_WINDOW_WIDTH */, &w); q(win, 1 /* NATIVE_WINDOW_HEIGHT */, &h); }
    if (w <= 0 || w > 4096) w = 1;
    if (h <= 0 || h > 4096) h = 1;
    GLint pa[] = { 0x3057 /* EGL_WIDTH */, w, 0x3056 /* EGL_HEIGHT */, h, EGL_NONE };
    return ((CreatePbufFn)aemu_split_resolve(IDX_eglCreatePbufferSurface))(dpy, config, pa);
}

/* ------------------------------------------------------------ EGL_NATIVE_VISUAL_ID
 *
 * The bridge leaves EGL_NATIVE_VISUAL_ID 0. 2.3's EGLUtils (RenderScript, so the Nexus live wallpapers and the
 * 3D all-apps list) picks the config whose visual id equals the window's pixel format and finds none: "couldn't
 * find an EGLConfig matching the screen format", no context, and the app crashes. The id is derived from the
 * channel sizes, as a real driver reports it: 565 -> RGB_565, 8888 -> RGBA_8888, 888 -> RGBX_8888.
 */
__attribute__((visibility("default"))) unsigned eglGetConfigAttrib(void *dpy, void *config, int attr, int *value) {
    GetAttrFn f = (GetAttrFn)aemu_split_resolve(IDX_eglGetConfigAttrib);
    config = cfg_in(dpy, config);
    unsigned ok = f(dpy, config, attr, value);
    if (ok && attr == 0x302E /* EGL_NATIVE_VISUAL_ID */ && value && *value == 0) {
        int r = 0, g = 0, b = 0, a = 0;
        f(dpy, config, 0x3024, &r); f(dpy, config, 0x3023, &g); f(dpy, config, 0x3022, &b); f(dpy, config, 0x3021, &a);
        if (r == 5 && g == 6 && b == 5 && a == 0) *value = 4;
        else if (r == 8 && g == 8 && b == 8) *value = a == 8 ? 1 : 2;
    }
    return ok;
}

/* EGL lets configs be NULL to count the matches (2.3's EGLUtils does that first); the bridge answers
 * EGL_BAD_CONFIG instead. The count comes from a call into a scratch array. */
__attribute__((visibility("default"))) unsigned eglChooseConfig(void *dpy, const GLint *attribs, void **configs, int size, int *num) {
    ChooseFn f = (ChooseFn)aemu_split_resolve(IDX_eglChooseConfig);
    unsigned ok;
    int n = 0;
    void *tmp[256];
    ok = configs ? f(dpy, attribs, configs, size, num) : f(dpy, attribs, tmp, 256, &n);
    if (!ok) {
        char m[300]; int k = 0;
        for (int i = 0; attribs && attribs[i] != 0x3038 && i < 40 && k < 280; i += 2)
            k += __builtin_snprintf(m + k, sizeof m - k, "%x=%d ", attribs[i], attribs[i + 1]);
        m[k] = 0;
        __android_log_print(5, "aemu-split", "eglChooseConfig refused: %s", m);
    }
    if (configs && ok && num) { intptr_t cur = cfg_base(dpy); for (int i = 0; i < *num; i++) configs[i] = cfg_out(dpy, configs[i], cur); }
    /* The bridge chooses among more configs than its eglGetConfigs lists (that stops at 64: 0x46.. came back
     * where the list ended at 0x41); 2.3's libEGL maps every chosen config through that list and failed the
     * whole call (EGL_BAD_CONFIG: RenderScript live wallpapers, Launcher2). EGL wants a subset: the rest is dropped. */
    if (configs && ok && num && *num > 0) {
        void *all[256]; int na = 0, m = 0;
        GetConfigsFn gc = (GetConfigsFn)aemu_split_resolve(IDX_eglGetConfigs);
        if (gc(dpy, all, 256, &na) && na > 0) {
            intptr_t cur = (intptr_t)all[0];
            for (int i = 0; i < na; i++) all[i] = cfg_out(dpy, all[i], cur);
            for (int i = 0; i < *num; i++) {
                int f = 0;
                for (int j = 0; j < na && !f; j++) f = all[j] == configs[i];
                if (f) configs[m++] = configs[i];
            }
            *num = m;
        }
    }
    if (configs) return ok;
    if (num) *num = ok ? n : 0;
    return ok;
}

__attribute__((visibility("default"))) unsigned eglGetConfigs(void *dpy, void **configs, int size, int *num) {
    if (!configs) {     /* counting (2.3's libEGL at eglInitialize): the bridge refuses NULL */
        void *tmp[256]; int n = 0;
        unsigned ok0 = ((GetConfigsFn)aemu_split_resolve(IDX_eglGetConfigs))(dpy, tmp, 256, &n);
        if (num) *num = ok0 ? n : 0;
        return ok0;
    }
    unsigned ok = ((GetConfigsFn)aemu_split_resolve(IDX_eglGetConfigs))(dpy, configs, size, num);
    if (ok && configs && num && *num > 0) { intptr_t cur = (intptr_t)configs[0]; for (int i = 0; i < *num; i++) configs[i] = cfg_out(dpy, configs[i], cur); }
    return ok;
}

__attribute__((visibility("default"))) void *eglCreatePbufferSurface(void *dpy, void *config, const GLint *attribs) {
    return ((CreatePbufFn)aemu_split_resolve(IDX_eglCreatePbufferSurface))(dpy, cfg_in(dpy, config), attribs);
}

/* 2.3's libEGL builds its config table only for a driver whose eglInitialize filled in the version (major >= 0);
 * the bridge leaves it untouched (-1): the table stayed empty and every eglChooseConfig gave EGL_BAD_CONFIG
 * (RenderScript, so the live wallpapers and the launcher died) */
__attribute__((visibility("default"))) unsigned eglInitialize(void *dpy, int *major, int *minor) {
    unsigned ok = ((unsigned (*)(void *, int *, int *))aemu_split_resolve(IDX_eglInitialize))(dpy, major, minor);
    if (ok) {
        if (major && *major < 1) *major = 1;
        if (minor && *minor < 0) *minor = 4;
    }
    return ok;
}

/* ------------------------------------------------------------ shader diagnostics: log only failures */
typedef void (*ObjFn)(unsigned);
typedef void (*GetivFn)(unsigned, GLenum, GLint *);
typedef void (*InfoLogFn)(unsigned, GLsizei, GLsizei *, char *);
static void gl_report(const char *what, unsigned id, int idx_iv, int idx_log, GLenum st) {
    GLint ok = 1; char buf[512]; GLsizei n = 0;
    ((GetivFn)aemu_split_resolve(idx_iv))(id, st, &ok);
    if (ok) return;
    buf[0] = 0;
    ((InfoLogFn)aemu_split_resolve(idx_log))(id, sizeof buf - 1, &n, buf);
    buf[n > 0 && n < 512 ? n : 0] = 0;
    __android_log_print(6, "aemu-split", "%s %u failed (es1 %d): %s", what, id, es1_current(), buf);
}
__attribute__((visibility("default"))) void glCompileShader(unsigned sh) {
    ((ObjFn)aemu_split_resolve(IDX_glCompileShader))(sh);
    gl_report("CompileShader", sh, IDX_glGetShaderiv, IDX_glGetShaderInfoLog, 0x8B81);
}
__attribute__((visibility("default"))) void glLinkProgram(unsigned p) {
    ((ObjFn)aemu_split_resolve(IDX_glLinkProgram))(p);
    gl_report("LinkProgram", p, IDX_glGetProgramiv, IDX_glGetProgramInfoLog, 0x8B82);
}
