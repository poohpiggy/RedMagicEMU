/*
 * gralloc.default.so for 32-bit firmwares on open32 (2.x-7.x): the engine's gralloc (libgralloc_kk.so) behind the
 * allocator layout the firmware's libui calls.
 *
 * AOSP alloc_device_t (hardware/libhardware/include/hardware/gralloc.h): common, alloc, free, dump. Some vendor
 * builds (HTC Sense 4.4, Qualcomm CAF libui) add a buffer size: GraphicBufferAllocator::alloc(w, h, format, usage,
 * handle, stride, bufferSize) calls slot 0x40 with the size and 0x44 without it, and ::free calls 0x48 (HTC One
 * 4.4.2 libui.so: alloc 0x6594, free 0x6718). With the AOSP table their alloc landed in free (SurfaceFlinger
 * SIGSEGV in the first buffer). The firmware's libui exporting the sized alloc
 * (_ZN7android22GraphicBufferAllocator5allocEjjiiPPK13native_handlePij) is the sign of that layout.
 *
 * Module functions are forwarded with the engine module's own pointer, so its private state stays its own.
 * ARMv7, libc/libdl only, classic hash table: loads under bionic 2.3-7.x.
 */
typedef unsigned int u32;
typedef unsigned short u16;

extern void *dlopen(const char *name, int flags);
extern void *dlsym(void *h, const char *name);
extern void *malloc(unsigned n);
extern void free(void *p);
extern int strcmp(const char *a, const char *b);
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);

struct hw_module;
struct hw_device {
    u32 tag, version;
    struct hw_module *module;
    u32 reserved[12];
    int (*close)(struct hw_device *d);
};
struct hw_module_methods { int (*open)(const struct hw_module *m, const char *id, struct hw_device **dev); };
struct hw_module {
    u32 tag;
    u16 module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct hw_module_methods *methods;
    void *dso;
    u32 reserved[25];
};
struct gr_module {
    struct hw_module common;
    int (*registerBuffer)(const struct gr_module *m, const void *h);
    int (*unregisterBuffer)(const struct gr_module *m, const void *h);
    int (*lock)(const struct gr_module *m, const void *h, int usage, int l, int t, int w, int hh, void **vaddr);
    int (*unlock)(const struct gr_module *m, const void *h);
    int (*perform)(const struct gr_module *m, int op, ...);
    int (*lock_ycbcr)(const struct gr_module *m, const void *h, int usage, int l, int t, int w, int hh, void *ycbcr);
    void *reserved_proc[6];
};
struct alloc_dev {
    struct hw_device common;
    int (*alloc)(struct alloc_dev *d, int w, int h, int format, int usage, const void **handle, int *stride);
    int (*free)(struct alloc_dev *d, const void *handle);
    void (*dump)(struct alloc_dev *d, char *buf, int len);
    void *reserved_proc[7];
};
/* the sized layout: allocSize, alloc, free, dump */
struct alloc_dev_sized {
    struct hw_device common;
    int (*alloc_size)(struct alloc_dev_sized *d, int w, int h, int format, int usage, const void **handle, int *stride, int size);
    int (*alloc)(struct alloc_dev_sized *d, int w, int h, int format, int usage, const void **handle, int *stride);
    int (*free)(struct alloc_dev_sized *d, const void *handle);
    void (*dump)(struct alloc_dev_sized *d, char *buf, int len);
    void *reserved_proc[7];
    struct alloc_dev *real;
};

static const struct gr_module *R;

static const struct gr_module *real(void) {
    if (!R) {
        void *h = dlopen("/system/lib/libgralloc_kk.so", 0);
        R = h ? (const struct gr_module *)dlsym(h, "HMI") : 0;
        if (!R) __android_log_print(6, "aemu-gralloc", "engine gralloc not loaded");
    }
    return R;
}

static int reg(const struct gr_module *m, const void *h) { (void)m; return real()->registerBuffer(R, h); }
static int unreg(const struct gr_module *m, const void *h) { (void)m; return real()->unregisterBuffer(R, h); }
static int lock(const struct gr_module *m, const void *h, int usage, int l, int t, int w, int hh, void **vaddr) {
    (void)m; return real()->lock(R, h, usage, l, t, w, hh, vaddr);
}
static int unlock(const struct gr_module *m, const void *h) { (void)m; return real()->unlock(R, h); }
static int perform(const struct gr_module *m, int op, int a, int b, int c, int d, int e, int f) {
    (void)m;
    return real()->perform ? R->perform(R, op, a, b, c, d, e, f) : -22;
}
static int lock_ycbcr(const struct gr_module *m, const void *h, int usage, int l, int t, int w, int hh, void *y) {
    (void)m;
    return real()->lock_ycbcr ? R->lock_ycbcr(R, h, usage, l, t, w, hh, y) : -22;
}

static int sized_alloc_size(struct alloc_dev_sized *d, int w, int h, int format, int usage, const void **handle, int *stride, int size) {
    (void)size;     /* the engine's buffers are w x h of the format: a bigger size only matters to vendor codecs */
    return d->real->alloc(d->real, w, h, format, usage, handle, stride);
}
static int sized_alloc(struct alloc_dev_sized *d, int w, int h, int format, int usage, const void **handle, int *stride) {
    return d->real->alloc(d->real, w, h, format, usage, handle, stride);
}
static int sized_free(struct alloc_dev_sized *d, const void *handle) { return d->real->free(d->real, handle); }
static void sized_dump(struct alloc_dev_sized *d, char *buf, int len) { if (d->real->dump) d->real->dump(d->real, buf, len); }
static int sized_close(struct hw_device *dev) {
    struct alloc_dev_sized *d = (struct alloc_dev_sized *)dev;
    int r = d->real->common.close(&d->real->common);
    free(d);
    return r;
}

static int sized_libui(void) {
    void *ui = dlopen("libui.so", 0);
    return ui && dlsym(ui, "_ZN7android22GraphicBufferAllocator5allocEjjiiPPK13native_handlePij") != 0;
}

extern struct gr_module HMI;

static int open_dev(const struct hw_module *m, const char *id, struct hw_device **dev) {
    (void)m;
    if (!real()) return -19;
    struct hw_device *d = 0;
    int r = R->common.methods->open(&R->common, id, &d);
    if (r || !d) return r ? r : -19;
    if (strcmp(id, "gpu0") == 0 && sized_libui()) {
        struct alloc_dev_sized *s = malloc(sizeof *s);
        if (!s) { d->close(d); return -12; }
        char *p = (char *)s; for (unsigned i = 0; i < sizeof *s; i++) p[i] = 0;
        s->common.tag = d->tag; s->common.version = d->version;
        s->common.module = &HMI.common;
        s->common.close = sized_close;
        s->alloc_size = sized_alloc_size; s->alloc = sized_alloc; s->free = sized_free; s->dump = sized_dump;
        s->real = (struct alloc_dev *)d;
        __android_log_print(4, "aemu-gralloc", "allocator with the sized layout (alloc 0x40/0x44, free 0x48)");
        d = &s->common;
    }
    *dev = d;
    return 0;
}

static struct hw_module_methods methods = { open_dev };

__attribute__((visibility("default"))) struct gr_module HMI = {
    .common = {
        .tag = 0x48574d54,                  /* HARDWARE_MODULE_TAG */
        .module_api_version = 1, .hal_api_version = 0,
        .id = "gralloc", .name = "AEmulator gralloc (engine's, firmware's layout)", .author = "AEmulator",
        .methods = &methods,
    },
    .registerBuffer = reg, .unregisterBuffer = unreg, .lock = lock, .unlock = unlock,
    .perform = (int (*)(const struct gr_module *, int, ...))perform, .lock_ycbcr = lock_ycbcr,
};
