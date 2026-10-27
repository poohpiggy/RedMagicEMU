/*
 * overlay.default.so — overlay HAL of AEmulator for 2.x firmwares whose camera shows its preview through an overlay
 * (Samsung 2.3: CameraService fails startPreview when no overlay can be created).
 *
 * Interface: hardware/overlay.h of 2.3, checked against the firmware's own users:
 *   control device (SurfaceFlinger, LayerBuffer::OverlaySource): get 0x40, createOverlay(w, h, format) 0x44,
 *     destroyOverlay 0x48, setPosition 0x4c, getPosition 0x50, setParameter 0x54, stage 0x58, commit 0x5c;
 *     overlay_t: w, h, format, w_stride, h_stride at 0..0x10, getHandleRef at 0x20
 *   data device (libui android::Overlay): initialize 0x40, resizeInput 0x44, setCrop 0x48, getCrop 0x4c,
 *     setParameter 0x50, dequeueBuffer 0x54, queueBuffer 0x58, getBufferAddress 0x5c, getBufferCount 0x60
 *
 * The picture: the control side tells the app where the overlay is (/dev/socket/aemu_ovl: u32 type 1 = position
 * id x y w h visible, type 3 = gone id). The data side gets "physical addresses" of camera frames that the
 * emulator's V4L2 driver handed out (0xA0000000 | fd << 16 | buffer << 8 | plane); it asks that driver, through a
 * private ioctl on the same fd, to send buffer <n> to the app as the frame of overlay <id>. The app draws it over
 * the guest's screen in the overlay's rectangle.
 *
 * Only libc is used.
 */
#include <stdint.h>
#include <stddef.h>

extern void *calloc(unsigned long n, unsigned long size);
extern void free(void *p);
extern int socket(int domain, int type, int proto);
extern int connect(int fd, const void *addr, unsigned len);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern int ioctl(int fd, int req, ...);
extern int getpid(void);
extern void *memcpy(void *d, const void *s, unsigned long n);
#define EXPORT __attribute__((visibility("default")))
#define HARDWARE_MODULE_TAG 0x484d4f44   /* HMOD */
#define HARDWARE_DEVICE_TAG 0x48574454   /* HWDT */
#define AEMU_OVL_SHOW 0x400856f0          /* _IOW('V', 0xf0, u32[2]): overlay id, buffer index (v4l2.c) */

struct hw_module;
struct hw_device;
struct hw_module_methods { int (*open)(const struct hw_module *m, const char *id, struct hw_device **dev); };
struct hw_module {
    uint32_t tag;
    uint16_t version_major, version_minor;
    const char *id, *name, *author;
    struct hw_module_methods *methods;
    void *dso;
    uint32_t reserved[25];
};
struct hw_device {
    uint32_t tag, version;
    struct hw_module *module;
    uint32_t reserved[12];
    int (*close)(struct hw_device *dev);
};
struct native_handle { int version, numFds, numInts, data[4]; };

struct overlay {
    uint32_t w, h; int32_t format; uint32_t w_stride, h_stride;
    uint32_t reserved[3];
    const struct native_handle *(*getHandleRef)(struct overlay *o);
    uint32_t reserved_procs[7];
    /* ours */
    struct native_handle handle;
    int x, y; uint32_t pw, ph; int shown;
    int tr;   /* HAL_TRANSFORM bits SurfaceFlinger set (OVERLAY_TRANSFORM) */
};

struct control_device {
    struct hw_device common;
    int (*get)(struct control_device *d, int name);
    struct overlay *(*createOverlay)(struct control_device *d, uint32_t w, uint32_t h, int32_t format);
    void (*destroyOverlay)(struct control_device *d, struct overlay *o);
    int (*setPosition)(struct control_device *d, struct overlay *o, int x, int y, uint32_t w, uint32_t h);
    int (*getPosition)(struct control_device *d, struct overlay *o, int *x, int *y, uint32_t *w, uint32_t *h);
    int (*setParameter)(struct control_device *d, struct overlay *o, int param, int value);
    int (*stage)(struct control_device *d, struct overlay *o);
    int (*commit)(struct control_device *d, struct overlay *o);
    int sock, next;
};

struct data_device {
    struct hw_device common;
    int (*initialize)(struct data_device *d, const struct native_handle *h);
    int (*resizeInput)(struct data_device *d, uint32_t w, uint32_t h);
    int (*setCrop)(struct data_device *d, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    int (*getCrop)(struct data_device *d, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
    int (*setParameter)(struct data_device *d, int param, int value);
    int (*dequeueBuffer)(struct data_device *d, void **buf);
    int (*queueBuffer)(struct data_device *d, void *buf);
    void *(*getBufferAddress)(struct data_device *d, void *buf);
    int (*getBufferCount)(struct data_device *d);
    int id; void *last; uint32_t cx, cy, cw, ch;
};

/* ---- control ---- */
static void send_msg(struct control_device *d, uint32_t type, const struct overlay *o, int visible) {
    if (d->sock < 0) {
        struct { uint16_t family; char path[108]; } sa = { 1, "/dev/socket/aemu_ovl" };
        int s = socket(1, 1 | 02000000, 0);
        if (s < 0) return;
        if (connect(s, &sa, 2 + 21) < 0) { close(s); return; }
        d->sock = s;
    }
    uint32_t m[7] = { type, (uint32_t)o->handle.data[0], (uint32_t)o->x, (uint32_t)o->y, o->pw, o->ph, (uint32_t)visible | (uint32_t)(o->tr & 7) << 8 };
    if (write(d->sock, m, sizeof m) != (long)sizeof m) { close(d->sock); d->sock = -1; }
}

static const struct native_handle *get_handle(struct overlay *o) { return &o->handle; }

static int ctl_get(struct control_device *d, int name) {
    (void)d;
    switch (name) {
    case 1: case 2: return 8;          /* minification / magnification limit */
    case 4: return 90;                 /* rotation step */
    case 3: return 0;                  /* scaling fraction bits */
    default: return 1;                 /* alignments */
    }
}
static struct overlay *ctl_create(struct control_device *d, uint32_t w, uint32_t h, int32_t format) {
    struct overlay *o = calloc(1, sizeof *o);
    if (!o) return 0;
    o->w = w; o->h = h; o->format = format; o->w_stride = w; o->h_stride = h;
    o->getHandleRef = get_handle;
    o->handle.version = 12; o->handle.numFds = 0; o->handle.numInts = 4;
    o->handle.data[0] = (getpid() << 8) | (++d->next & 0xff);
    o->handle.data[1] = (int)w; o->handle.data[2] = (int)h; o->handle.data[3] = format;
    return o;
}
static void ctl_destroy(struct control_device *d, struct overlay *o) { if (!o) return; send_msg(d, 3, o, 0); free(o); }
static int ctl_set_pos(struct control_device *d, struct overlay *o, int x, int y, uint32_t w, uint32_t h) {
    (void)d; o->x = x; o->y = y; o->pw = w; o->ph = h; return 0;
}
static int ctl_get_pos(struct control_device *d, struct overlay *o, int *x, int *y, uint32_t *w, uint32_t *h) {
    (void)d; *x = o->x; *y = o->y; *w = o->pw; *h = o->ph; return 0;
}
/* hardware/overlay.h (2.3): OVERLAY_ROTATION_DEG 1, OVERLAY_TRANSFORM 4 with HAL_TRANSFORM_* values;
 * LayerBuffer::OverlaySource sets the transform from the orientation CameraService created the overlay with */
static int ctl_set_param(struct control_device *d, struct overlay *o, int p, int v) {
    (void)d;
    if (p == 4) o->tr = v & 7;
    else if (p == 1) o->tr = v == 90 ? 4 : v == 180 ? 3 : v == 270 ? 7 : 0;
    return 0;
}
static int ctl_stage(struct control_device *d, struct overlay *o) { (void)d; (void)o; return 0; }
static int ctl_commit(struct control_device *d, struct overlay *o) { send_msg(d, 1, o, o->pw && o->ph); return 0; }

/* ---- data ---- */
static int dat_init(struct data_device *d, const struct native_handle *h) {
    d->id = h && h->numInts >= 1 ? h->data[h->numFds] : 0;
    return 0;
}
static int dat_resize(struct data_device *d, uint32_t w, uint32_t h) { (void)d; (void)w; (void)h; return 0; }
static int dat_set_crop(struct data_device *d, uint32_t x, uint32_t y, uint32_t w, uint32_t h) { d->cx = x; d->cy = y; d->cw = w; d->ch = h; return 0; }
static int dat_get_crop(struct data_device *d, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) { *x = d->cx; *y = d->cy; *w = d->cw; *h = d->ch; return 0; }
static int dat_set_param(struct data_device *d, int p, int v) { (void)d; (void)p; (void)v; return 0; }
static int dat_dequeue(struct data_device *d, void **buf) { if (buf) *buf = d->last; return 0; }
static int dat_queue(struct data_device *d, void *buf) {
    uint32_t t = (uint32_t)(uintptr_t)buf;
    /* the token itself, or a pointer to {Y, CbCr} tokens (Samsung's nativeOverlay passes the latter) */
    if ((t & 0xf0000000u) != 0xa0000000u && buf) memcpy(&t, buf, 4);
    d->last = buf;
    if ((t & 0xf0000000u) != 0xa0000000u) return 0;
    uint32_t arg[2] = { (uint32_t)d->id, (t >> 8) & 0xff };
    ioctl((int)((t >> 16) & 0xfff), AEMU_OVL_SHOW, arg);
    return 0;
}
static void *dat_addr(struct data_device *d, void *buf) { (void)d; (void)buf; return 0; }
static int dat_count(struct data_device *d) { (void)d; return 0; }

static int dev_close(struct hw_device *h) {
    if (h && h->version == 1) { struct control_device *c = (struct control_device *)h; if (c->sock >= 0) close(c->sock); }
    free(h);
    return 0;
}

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static int ovl_open(const struct hw_module *m, const char *id, struct hw_device **out) {
    if (id && streq(id, "control")) {
        struct control_device *c = calloc(1, sizeof *c);
        if (!c) return -12;
        c->common.tag = HARDWARE_DEVICE_TAG; c->common.version = 1; c->common.module = (struct hw_module *)m;
        c->common.close = dev_close;
        c->get = ctl_get; c->createOverlay = ctl_create; c->destroyOverlay = ctl_destroy; c->setPosition = ctl_set_pos;
        c->getPosition = ctl_get_pos; c->setParameter = ctl_set_param; c->stage = ctl_stage; c->commit = ctl_commit;
        c->sock = -1;
        *out = &c->common;
        return 0;
    }
    if (id && streq(id, "data")) {
        struct data_device *d = calloc(1, sizeof *d);
        if (!d) return -12;
        d->common.tag = HARDWARE_DEVICE_TAG; d->common.version = 0; d->common.module = (struct hw_module *)m;
        d->common.close = dev_close;
        d->initialize = dat_init; d->resizeInput = dat_resize; d->setCrop = dat_set_crop; d->getCrop = dat_get_crop;
        d->setParameter = dat_set_param; d->dequeueBuffer = dat_dequeue; d->queueBuffer = dat_queue;
        d->getBufferAddress = dat_addr; d->getBufferCount = dat_count;
        *out = &d->common;
        return 0;
    }
    return -22;
}

static struct hw_module_methods methods = { ovl_open };

EXPORT struct hw_module HMI = {
    HARDWARE_MODULE_TAG, 1, 0, "overlay", "AEmulator overlay", "AEmulator", &methods, 0, { 0 },
};
