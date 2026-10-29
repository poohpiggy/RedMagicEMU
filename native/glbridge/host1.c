/*
 * The host side of the GLES 1 bridge of Android 1.x (guest1.c, gen1.py), part of host.c (included at its end; the
 * connection, its input buffer and replies are host.c's). A connection that says hello with AEMU_GL1_PROTOCOL is a
 * drawing thread of a 1.x program: its contexts are GLES 1.1 contexts of the phone (Android's CDD keeps GLES 1.1 on
 * every device), each guest surface a pbuffer of its size. A swap reads the pbuffer back in the guest window's format.
 * GLES 1 entry points come from the phone's libGLESv1_CM.so (by name, as the guest's libEGL does).
 */
#include <dlfcn.h>
#include "gl1_common.h"
/* GLES 1 names the GLES 3 headers lack */
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define EGL_OPENGL_ES_BIT 0x0001
typedef int GLclampx;
#include "gl1_api.h"

static EGLConfig g1_cfg;
static int g1_cfg_ok;       /* 0 not looked for, 1 found, -1 none */
struct ctx1 { uint32_t id; int pid; EGLContext egl; EGLSurface pb1; struct ctx1 *next; };
struct surf1 { uint32_t key; int pid, w, h; EGLSurface pb; struct surf1 *next; };
static struct ctx1 *g1_ctx;
static struct surf1 *g1_surf;
static __thread struct ctx1 *C1;
static __thread struct surf1 *S1;
static __thread uint8_t *g1_px;
static __thread uint32_t g1_on;     /* the arrays this thread's host context has enabled (state changes only) */
static __thread size_t g1_px_cap;

#define scratch1 scratch
#define reply1 reply
#define reply1_32 reply32
static void h1_hand(uint32_t op, const uint8_t *p, uint32_t len);
#include "h1_dec.h"

static long nop1(void) { return 0; }
static int g1_load(void) {
    static int done;
    if (done) return done > 0;
    void *lib = dlopen("libGLESv1_CM.so", RTLD_NOW);
    int miss = 0;
    void **slot = (void **)&F1;
    for (int i = 0; i < N1_FUNCS; i++) {
        void *f = lib ? dlsym(lib, kNames1[i]) : 0;
        if (!f) f = (void *)eglGetProcAddress(kNames1[i]);
        if (!f) { miss++; f = (void *)nop1; }   /* (a call of it does nothing and gives 0) */
        slot[i] = f;
    }
    if (miss) LOG("GLES 1: %d entry points missing on this phone", miss);
    done = lib ? 1 : -1;
    return done > 0;
}
static int g1_config(void) {
    static const EGLint want[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    pthread_mutex_lock(&g_lock);
    if (!g1_cfg_ok) {
        EGLint n = 0;
        g1_cfg_ok = g_dpy && eglChooseConfig(g_dpy, want, &g1_cfg, 1, &n) && n > 0 ? 1 : -1;
        if (g1_cfg_ok < 0) LOG("GLES 1: the phone has no GLES 1 pbuffer config");
    }
    pthread_mutex_unlock(&g_lock);
    return g1_cfg_ok > 0 && g1_load();
}
static struct ctx1 *ctx1_find(uint32_t id) {
    struct ctx1 *x;
    pthread_mutex_lock(&g_lock);
    for (x = g1_ctx; x && x->id != id; x = x->next) {}
    pthread_mutex_unlock(&g_lock);
    return x;
}
static uint32_t ctx1_create(uint32_t share) {
    static const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE }, pa[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    struct ctx1 *sh = share ? ctx1_find(share) : 0, *x;
    if (!g1_config() || (share && !sh) || !(x = calloc(1, sizeof(*x)))) return 0;
    x->egl = eglCreateContext(g_dpy, g1_cfg, sh ? sh->egl : EGL_NO_CONTEXT, ca);
    x->pb1 = x->egl != EGL_NO_CONTEXT ? eglCreatePbufferSurface(g_dpy, g1_cfg, pa) : EGL_NO_SURFACE;
    if (x->egl == EGL_NO_CONTEXT || x->pb1 == EGL_NO_SURFACE) {
        LOG("GLES 1: no context from the phone's driver: 0x%x", eglGetError());
        if (x->egl != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, x->egl);
        free(x);
        return 0;
    }
    x->pid = C->pid;
    pthread_mutex_lock(&g_lock);
    x->id = ++g_ids;
    x->next = g1_ctx; g1_ctx = x;
    pthread_mutex_unlock(&g_lock);
    return x->id;
}
static void ctx1_destroy(uint32_t id) {
    pthread_mutex_lock(&g_lock);
    struct ctx1 **pp = &g1_ctx, *x = 0;
    while (*pp && (*pp)->id != id) pp = &(*pp)->next;
    if (*pp) { x = *pp; *pp = x->next; }
    pthread_mutex_unlock(&g_lock);
    if (!x) return;
    if (C1 == x) { eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); C1 = 0; S1 = 0; }
    eglDestroySurface(g_dpy, x->pb1);
    eglDestroyContext(g_dpy, x->egl);
    free(x);
}
static struct surf1 *surf1_get(uint32_t key, int w, int h) {
    static const int lim = 4096;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > lim) w = lim;
    if (h > lim) h = lim;
    pthread_mutex_lock(&g_lock);
    struct surf1 *s;
    for (s = g1_surf; s && !(s->key == key && s->pid == C->pid); s = s->next) {}
    if (!s && (s = calloc(1, sizeof(*s)))) { s->key = key; s->pid = C->pid; s->next = g1_surf; g1_surf = s; }
    pthread_mutex_unlock(&g_lock);
    if (!s) return 0;
    if (!s->pb || s->w != w || s->h != h) {
        EGLint pa[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
        EGLSurface pb = eglCreatePbufferSurface(g_dpy, g1_cfg, pa);
        if (pb == EGL_NO_SURFACE) { LOG("GLES 1: no %dx%d pbuffer: 0x%x", w, h, eglGetError()); return s->pb ? s : 0; }
        if (s->pb) {
            if (S1 == s) eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(g_dpy, s->pb);
        }
        s->pb = pb; s->w = w; s->h = h;
    }
    return s;
}
static void surf1_destroy(uint32_t key) {
    pthread_mutex_lock(&g_lock);
    struct surf1 **pp = &g1_surf, *s = 0;
    while (*pp && !((*pp)->key == key && (*pp)->pid == C->pid)) pp = &(*pp)->next;
    if (*pp) { s = *pp; *pp = s->next; }
    pthread_mutex_unlock(&g_lock);
    if (!s) return;
    if (S1 == s) { if (C1) eglMakeCurrent(g_dpy, C1->pb1, C1->pb1, C1->egl); S1 = 0; }
    eglDestroySurface(g_dpy, s->pb);
    free(s);
}
static uint32_t make_current1(uint32_t id, uint32_t key, int w, int h) {
    struct ctx1 *x = ctx1_find(id);
    if (!x) return 0;
    struct surf1 *s = key ? surf1_get(key, w, h) : 0;
    EGLSurface d = s ? s->pb : x->pb1;
    if (!eglMakeCurrent(g_dpy, d, d, x->egl)) { LOG("GLES 1: make current: 0x%x", eglGetError()); return 0; }
    if (C1 != x) g1_on = 0;   /* (another context: its own array state, all off when new) */
    C1 = x; S1 = s;
    return 1;
}

/* the frame, in the guest window's pixel format, top row first */
static void swap1(int w, int h, uint32_t fmt) {
    size_t n = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0);
    uint32_t bpp = fmt == 4 ? 2 : 4;
    if (g1_px_cap < n * 4) { free(g1_px); g1_px = malloc(n * 4); g1_px_cap = g1_px ? n * 4 : 0; }
    uint8_t *out = scratch((uint32_t)(n * bpp));
    if (!g1_px || !out) { reply(out, (uint32_t)(n * bpp)); return; }
    if (C1 && S1) {
        if (w > S1->w) w = S1->w;
        if (h > S1->h) h = S1->h;
        F1.glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g1_px);
    } else memset(g1_px, 0, n * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = g1_px + (size_t)(h - 1 - y) * w * 4;
        uint8_t *d = out + (size_t)y * w * bpp;
        if (fmt == 4) {
            uint16_t *o = (uint16_t *)d;
            for (int x = 0; x < w; x++, s += 4) o[x] = (uint16_t)(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
        } else if (fmt == 5) {
            for (int x = 0; x < w; x++, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
        } else memcpy(d, s, (size_t)w * 4);
    }
    reply(out, (uint32_t)(n * bpp));
    C->frames++;
}

static const char *const kExt1[] = {
    "GL_OES_byte_coordinates", "GL_OES_fixed_point", "GL_OES_single_precision", "GL_OES_read_format",
    "GL_OES_compressed_paletted_texture", "GL_OES_draw_texture", "GL_OES_matrix_get", "GL_OES_point_size_array",
    "GL_OES_point_sprite", "GL_OES_compressed_ETC1_RGB8_texture", "GL_OES_blend_equation_separate",
    "GL_OES_blend_func_separate", "GL_OES_blend_subtract", "GL_OES_framebuffer_object", "GL_OES_texture_npot",
    "GL_OES_rgb8_rgba8", "GL_OES_depth24", "GL_OES_stencil8", "GL_OES_element_index_uint", "GL_OES_texture_env_crossbar",
    "GL_OES_texture_mirrored_repeat", "GL_EXT_texture_format_BGRA8888", "GL_EXT_texture_filter_anisotropic",
};
static void get_string1(GLenum name) {
    static char ext[1024], ren[256];
    const char *s = 0;
    if (name == GL_EXTENSIONS) {
        if (!ext[0]) {
            const char *all = (const char *)F1.glGetString(GL_EXTENSIONS);
            for (unsigned i = 0; all && i < sizeof(kExt1) / sizeof(kExt1[0]); i++) {
                size_t n = strlen(kExt1[i]);
                for (const char *f = strstr(all, kExt1[i]); f; f = strstr(f + 1, kExt1[i]))
                    if ((f == all || f[-1] == ' ') && (f[n] == ' ' || !f[n])) {
                        strcat(ext, kExt1[i]); strcat(ext, " ");
                        /* (Android 1.x's SurfaceFlinger takes non-power-of-two textures only by this name) */
                        if (!strcmp(kExt1[i], "GL_OES_texture_npot")) strcat(ext, "GL_ARB_texture_non_power_of_two ");
                        break;
                    }
            }
        }
        s = ext;
    } else if (name == GL_VENDOR) s = "AEmulator";
    else if (name == GL_RENDERER) {
        if (!ren[0]) { const char *r = (const char *)F1.glGetString(GL_RENDERER); snprintf(ren, sizeof(ren), "AEmulator GPU bridge (%s)", r ? r : "?"); }
        s = ren;
    } else if (name == GL_VERSION) s = "OpenGL ES-CM 1.1";
    if (!s) { reply32(NIL); return; }
    uint32_t n = (uint32_t)strlen(s), pad = (4 - (n & 3)) & 3;
    reply32(n); reply(s, n); reply("\0\0\0", pad);
}

/* a draw (guest1.c draw()): arrays from the guest's memory are kept here and pointed at as client arrays */
static __thread struct { uint8_t *p; uint32_t cap; } g1_arr[A1_N + 1];
static const void *keep1(int i, const void *d, uint32_t n) {
    if (g1_arr[i].cap < n) { free(g1_arr[i].p); g1_arr[i].p = malloc(n); g1_arr[i].cap = g1_arr[i].p ? n : 0; }
    if (g1_arr[i].p && d) memcpy(g1_arr[i].p, d, n);
    return g1_arr[i].p;
}
static uint32_t tsize1(uint32_t type) { return type == 0x1400 || type == 0x1401 ? 1 : type == 0x1402 || type == 0x1403 ? 2 : 4; }
static void draw1(const uint8_t *p) {
    GLenum mode = rd32(&p); GLsizei count = (GLsizei)rd32(&p); GLint first = (GLint)rd32(&p); GLenum itype = rd32(&p);
    uint32_t ioff = rd32(&p), in = 0;
    const void *ip = 0;
    if (itype && ioff == NIL) { ip = rdblob(&p, &in); if (ip) ip = keep1(A1_N, ip, in); }
    else ip = (const void *)(uintptr_t)ioff;
    GLuint array_buf = rd32(&p); uint32_t mask = rd32(&p);
    for (int i = 0; i < A1_N; i++) {
        GLenum cap = i == A1_VERTEX ? GL_VERTEX_ARRAY : i == A1_COLOR ? GL_COLOR_ARRAY : i == A1_NORMAL ? GL_NORMAL_ARRAY :
                     i == A1_POINT_SIZE ? 0x8B9C /* POINT_SIZE_ARRAY_OES */ : GL_TEXTURE_COORD_ARRAY;
        int on = (mask >> i) & 1, was = (g1_on >> i) & 1;
        if (!on && !was) continue;
        if (i >= A1_TEXCOORD0) F1.glClientActiveTexture(GL_TEXTURE0 + (i - A1_TEXCOORD0));
        if (!on) { F1.glDisableClientState(cap); g1_on &= ~(1u << i); continue; }
        GLint size = (GLint)rd32(&p); GLenum type = rd32(&p); GLsizei stride = (GLsizei)rd32(&p); GLuint buf = rd32(&p);
        uint32_t lo = rd32(&p), es = stride ? (uint32_t)stride : (uint32_t)size * tsize1(type);
        const void *ptr;
        if (buf) { ptr = (const void *)(uintptr_t)rd32(&p); F1.glBindBuffer(GL_ARRAY_BUFFER, buf); }
        else {
            uint32_t n; const void *d = rdblob(&p, &n);
            if (!d) { F1.glDisableClientState(cap); g1_on &= ~(1u << i); continue; }
            ptr = (const uint8_t *)keep1(i, d, n) - (size_t)lo * es;
            F1.glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (!was) { F1.glEnableClientState(cap); g1_on |= 1u << i; }
        switch (i) {
        case A1_VERTEX: F1.glVertexPointer(size, type, stride, ptr); break;
        case A1_COLOR: F1.glColorPointer(size, type, stride, ptr); break;
        case A1_NORMAL: F1.glNormalPointer(type, stride, ptr); break;
        case A1_POINT_SIZE: if (F1.glPointSizePointerOES) F1.glPointSizePointerOES(type, stride, ptr); break;
        default: F1.glTexCoordPointer(size, type, stride, ptr); break;
        }
    }
    F1.glBindBuffer(GL_ARRAY_BUFFER, array_buf);
    if (itype) F1.glDrawElements(mode, count, itype, ip); else F1.glDrawArrays(mode, first, count);
}
static void h1_hand(uint32_t op, const uint8_t *p, uint32_t len) {
    (void)len;
    switch (op) {
    case OP1_CTX_CREATE: reply32(ctx1_create(rd32(&p))); break;
    case OP1_CTX_DESTROY: ctx1_destroy(rd32(&p)); break;
    case OP1_MAKE_CURRENT: { uint32_t id = rd32(&p), key = rd32(&p); int w = (int)rd32(&p), h = (int)rd32(&p); reply32(make_current1(id, key, w, h)); break; }
    case OP1_RELEASE: eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); C1 = 0; S1 = 0; reply32(1); break;
    case OP1_SURF_DESTROY: surf1_destroy(rd32(&p)); break;
    case OP1_SWAP: { int w = (int)rd32(&p), h = (int)rd32(&p); swap1(w, h, rd32(&p)); break; }
    case OP1_DRAW: if (C1) draw1(p); break;
    case OP1_GET_STRING: get_string1(rd32(&p)); break;
    case OP1_TEX_IMAGE: {
        uint32_t sub = rd32(&p), v[10];
        for (int i = 0; i < 10; i++) v[i] = rd32(&p);
        uint32_t n; const void *d = rdblob(&p, &n);
        if (!C1) break;
        if (sub) F1.glTexSubImage2D(v[0], (GLint)v[1], (GLint)v[2], (GLint)v[3], (GLsizei)v[5], (GLsizei)v[6], v[8], v[9], d);
        else F1.glTexImage2D(v[0], (GLint)v[1], (GLint)v[2], (GLsizei)v[5], (GLsizei)v[6], (GLint)v[7], v[8], v[9], d);
        break;
    }
    case OP1_READ_PIXELS: {
        GLint x = (GLint)rd32(&p), y = (GLint)rd32(&p); GLsizei w = (GLsizei)rd32(&p), h = (GLsizei)rd32(&p);
        GLenum f = rd32(&p), t = rd32(&p); uint32_t n = rd32(&p);
        void *out = scratch(n);
        if (out) { memset(out, 0, n); if (C1) F1.glReadPixels(x, y, w, h, f, t, out); }
        reply(out, n);
        break;
    }
    case OP1_GET: {
        uint32_t kind = rd32(&p); GLenum pname = rd32(&p); uint32_t cnt = rd32(&p);
        if (cnt > 64) cnt = 64;
        uint32_t v[64]; GLboolean b[64];
        memset(v, 0, sizeof(v)); memset(b, 0, sizeof(b));
        if (C1) {
            if (kind == 0) { F1.glGetBooleanv(pname, b); for (uint32_t i = 0; i < cnt; i++) v[i] = b[i]; }
            else if (kind == 1) F1.glGetFloatv(pname, (GLfloat *)v);
            else if (kind == 2) F1.glGetIntegerv(pname, (GLint *)v);
            else F1.glGetFixedv(pname, (GLfixed *)v);
        }
        reply(v, cnt * 4);
        break;
    }
    case OP1_BUFFER_DATA: {
        uint32_t sub = rd32(&p); GLenum target = rd32(&p); uint32_t a = rd32(&p), size = rd32(&p), n;
        const void *d = rdblob(&p, &n);
        if (!C1) break;
        if (sub) F1.glBufferSubData(target, (GLintptr)a, (GLsizeiptr)size, d);
        else F1.glBufferData(target, (GLsizeiptr)size, d, a);
        break;
    }
    case OP1_FINISH: if (C1) F1.glFinish(); reply32(1); break;
    default: if (C->unknown++ < 10) LOG("GLES 1: unknown call %u", op); break;
    }
}

/* the loop of a GLES 1 connection (host.c serve() hands it over after the hello) */
static void serve1(void) {
    LOG("GLES 1 client, pid %d", C->pid);
    while (C->in && !C->dead && need(8)) {
        uint32_t op, len;
        memcpy(&op, C->in + C->in_a, 4); memcpy(&len, C->in + C->in_a + 4, 4);
        if (len > (256u << 20) || !need(8 + (size_t)len)) break;
        const uint8_t *p = C->in + C->in_a + 8;
        if (op < N1_FUNCS) { if (C1) h1_decode(op, p, len); else if (C->unknown++ < 10) LOG("GLES 1 call %u with no context", op); }
        else h1_hand(op, p, len);
        /* debugging: with files/gltrace present every call that leaves a GL error is logged */
        static unsigned trace_n1 = 0;
        if ((trace_n1++ & 1023) == 0) trace_on = access("/data/data/app.aemu/files/gltrace", F_OK) == 0;
        if (trace_on && C1) {
            GLenum e = F1.glGetError();
            uint32_t w[3] = { 0 };
            memcpy(w, p, len < sizeof(w) ? len : sizeof(w));
            if (e) __android_log_print(ANDROID_LOG_DEBUG, "aemu-gltrace", "GLES 1 %s: error 0x%x (%x %x %x)",
                                       op < N1_FUNCS ? kNames1[op] : "(op)", e, w[0], w[1], w[2]);
        }
        C->in_a += 8 + (size_t)len;
        while (C->nscr) free(C->scr[--C->nscr]);
        reply_flush();
    }
    if (C->frames) LOG("a GLES 1 drawing thread of pid %d is gone after %lu frames", C->pid, C->frames);
    if (C1) eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    C1 = 0; S1 = 0;
    for (int i = 0; i <= A1_N; i++) { free(g1_arr[i].p); g1_arr[i].p = 0; g1_arr[i].cap = 0; }
    free(g1_px); g1_px = 0; g1_px_cap = 0;
}

/* the program's last drawing thread is gone (host.c pid_count): its contexts and surfaces with it */
static void g1_pid_gone(int pid) {
    for (;;) {
        struct ctx1 *x;
        pthread_mutex_lock(&g_lock);
        for (x = g1_ctx; x && x->pid != pid; x = x->next) {}
        uint32_t id = x ? x->id : 0;
        pthread_mutex_unlock(&g_lock);
        if (!id) break;
        ctx1_destroy(id);
    }
    for (;;) {
        struct surf1 *s;
        pthread_mutex_lock(&g_lock);
        for (s = g1_surf; s && s->pid != pid; s = s->next) {}
        uint32_t key = s ? s->key : 0;
        pthread_mutex_unlock(&g_lock);
        if (!key) break;
        int me = C->pid; C->pid = pid; surf1_destroy(key); C->pid = me;
    }
}
