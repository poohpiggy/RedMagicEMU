/*
 * libcamera.so for Android 3.x firmwares whose CameraService links the camera HAL directly (Motorola Xoom / Xoom 2,
 * Samsung Galaxy Tab 10.1, Lenovo K1, other Tegra 2 and OMAP4 tablets): the phone's back camera through the
 * emulator's /dev/socket/aemu_cam (CameraFeed). The firmware's own HAL drives the board's sensor and fails here.
 *
 * Interface: CameraHardwareInterface of 3.x (no AOSP source of Honeycomb was published). The slot order is the one of
 * the base class vtable (172 bytes, the same in all of them) as the firmwares' own HALs fill it, read from their
 * symbols: Xoom 3.2 libcamera.so android::NvOmxCamera, Xoom 2 3.2.2 android::CameraHal (TI OMAP4):
 *   +0x0c setPreviewWindow(sp<ANativeWindow>), +0x10 getRawHeap, +0x14 setCallbacks, ... +0x24 startPreview,
 *   +0x30 getNumberOfVideoBuffers, +0x34 getVideoBuffer, +0x38 storeMetaDataInBuffers, ... +0x6c dump.
 * Entry points: HAL_getNumberOfCameras / HAL_getCameraInfo / HAL_openCameraHardware; Samsung's CameraService imports
 * the same three as SEC_* (Galaxy Tab 10.1 3.1 libcameraservice.so).
 *
 * The preview is drawn by the HAL into the window CameraService gives it (as 4.0's camera_device preview_stream_ops,
 * which wrap the same ANativeWindow): the host sends RGBA_8888 frames (type 1, followed by NV21 for the
 * application's callbacks); each one is copied into a dequeued window buffer locked through the gralloc module.
 */
#include <stddef.h>
#include <stdint.h>

extern "C" {
int socket(int, int, int);
int connect(int, const void *, unsigned);
long read(int, void *, unsigned long);
long write(int, const void *, unsigned long);
int close(int);
int shutdown(int, int);
void *malloc(unsigned long);
void free(void *);
void *memcpy(void *, const void *, unsigned long);
void *memset(void *, int, unsigned long);
unsigned long strlen(const char *);
int snprintf(char *, unsigned long, const char *, ...);
int pthread_create(long *, const void *, void *(*)(void *), void *);
int pthread_join(long, void **);
int pthread_detach(long);
int pthread_mutex_lock(void *);
int pthread_mutex_unlock(void *);
int clock_gettime(int, void *);
int atoi(const char *);
int __android_log_print(int, const char *, const char *, ...);
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
}
void *operator new(size_t);
void operator delete(void *);
inline void *operator new(size_t, void *p) { return p; }

#define EXPORT __attribute__((visibility("default")))
#define LOG(...) __android_log_print(4, "aemu-camera", __VA_ARGS__)

namespace android {

class RefBase {
public:
    void incStrong(const void *id) const;
    void decStrong(const void *id) const;
protected:
    RefBase();
    virtual ~RefBase();
    virtual void onFirstRef();
    virtual void onLastStrongRef(const void *id);
    virtual bool onIncStrongAttempted(uint32_t flags, const void *id);
    virtual void onLastWeakRef(const void *id);
private:
    void *const mRefs;
};

template <class T> class sp {
public:
    sp() : m_ptr(0) {}
    sp(T *p) : m_ptr(p) { if (p) p->incStrong(this); }
    sp(const sp &o) : m_ptr(o.m_ptr) { if (m_ptr) m_ptr->incStrong(this); }
    ~sp() { if (m_ptr) m_ptr->decStrong(this); }
    sp &operator=(const sp &o) { T *p = o.m_ptr; if (p) p->incStrong(this); if (m_ptr) m_ptr->decStrong(this); m_ptr = p; return *this; }
    T *get() const { return m_ptr; }
private:
    T *m_ptr;
};

class String8 {
public:
    String8(const char *s);
    ~String8();
    const char *string() const { return mString; }
private:
    const char *mString;
};

class IBinder : public virtual RefBase {};
class BBinder : public IBinder {};
class IInterface : public virtual RefBase {};
class IMemoryHeap : public IInterface {};
class IMemory : public IInterface {};
template <class I> class BnInterface : public I, public BBinder {};
class BnMemoryHeap : public BnInterface<IMemoryHeap> {};
class BnMemory : public BnInterface<IMemory> {};
class MemoryHeapBase : public virtual BnMemoryHeap {
public:
    MemoryHeapBase(size_t size, uint32_t flags, const char *name);
    void *getBase() const;
};
class MemoryBase : public BnMemory {
public:
    MemoryBase(const sp<IMemoryHeap> &heap, long offset, size_t size);
};

class CameraParameters {
public:
    CameraParameters();
    ~CameraParameters();
    String8 flatten() const;
    void unflatten(const String8 &params);
    void set(const char *key, const char *value);
private:
    char m_[24];   /* DefaultKeyedVector<String8, String8>; only ever made in the caller's place */
};

typedef void (*notify_callback)(int32_t msg, int32_t ext1, int32_t ext2, void *user);
typedef void (*data_callback)(int32_t msg, const sp<IMemory> &data, void *user);
typedef void (*data_callback_timestamp)(int64_t ts, int32_t msg, const sp<IMemory> &data, void *user);

struct native_base { int magic, version; void *reserved[4]; void (*incRef)(native_base *); void (*decRef)(native_base *); };
/* system/window.h before 4.2: one set of buffer calls, lockBuffer between dequeue and queue */
struct ANativeWindowBuffer {
    native_base common;
    int width, height, stride, format, usage;
    void *reserved[2];
    const void *handle;
};
struct ANativeWindow {
    native_base common;
    uint32_t flags;
    int minSwapInterval, maxSwapInterval;
    float xdpi, ydpi;
    intptr_t oem[4];
    int (*setSwapInterval)(ANativeWindow *, int);
    int (*dequeueBuffer)(ANativeWindow *, ANativeWindowBuffer **);
    int (*lockBuffer)(ANativeWindow *, ANativeWindowBuffer *);
    int (*queueBuffer)(ANativeWindow *, ANativeWindowBuffer *);
    int (*query)(const ANativeWindow *, int what, int *value);
    int (*perform)(ANativeWindow *, int op, ...);
    int (*cancelBuffer)(ANativeWindow *, ANativeWindowBuffer *);
    void incStrong(const void *) { common.incRef(&common); }
    void decStrong(const void *) { common.decRef(&common); }
};
/* hardware/gralloc.h: gralloc_module_t after hw_module_t (32 words) */
struct gr_module {
    uint32_t tag; uint16_t mver, hver; const char *id, *name, *author; void *methods, *dso;
    uint32_t reserved[32 - 7];
    int (*registerBuffer)(const gr_module *, const void *);
    int (*unregisterBuffer)(const gr_module *, const void *);
    int (*lock)(const gr_module *, const void *, int usage, int l, int t, int w, int h, void **vaddr);
    int (*unlock)(const gr_module *, const void *);
};

class CameraHardwareInterface : public virtual RefBase {
public:
    virtual ~CameraHardwareInterface() {}
    virtual int setPreviewWindow(const sp<ANativeWindow> &) = 0;               /* 0x0c */
    virtual sp<IMemoryHeap> getRawHeap() const = 0;
    virtual void setCallbacks(notify_callback, data_callback, data_callback_timestamp, void *) = 0;   /* 0x14 */
    virtual void enableMsgType(int32_t) = 0;
    virtual void disableMsgType(int32_t) = 0;
    virtual bool msgTypeEnabled(int32_t) = 0;
    virtual int startPreview() = 0;                                           /* 0x24 */
    virtual void stopPreview() = 0;
    virtual bool previewEnabled() = 0;
    virtual int getNumberOfVideoBuffers() const = 0;                          /* 0x30 */
    virtual sp<IMemory> getVideoBuffer(int) const = 0;
    virtual int storeMetaDataInBuffers(bool) = 0;
    virtual int startRecording() = 0;                                         /* 0x3c */
    virtual void stopRecording() = 0;
    virtual bool recordingEnabled() = 0;
    virtual void releaseRecordingFrame(const sp<IMemory> &) = 0;
    virtual int autoFocus() = 0;                                              /* 0x4c */
    virtual int cancelAutoFocus() = 0;
    virtual int takePicture() = 0;
    virtual int cancelPicture() = 0;
    virtual int setParameters(const CameraParameters &) = 0;                  /* 0x5c */
    virtual CameraParameters getParameters() const = 0;
    virtual int sendCommand(int32_t, int32_t, int32_t) = 0;
    virtual void release() = 0;
    virtual int dump(int fd, const void *args) const = 0;                     /* 0x6c */
};

}  // namespace android

using namespace android;

namespace {

enum { W = 640, H = 480, RGBA_SIZE = W * H * 4, NV21_SIZE = W * H * 3 / 2, NBUF = 6 };
enum { MSG_SHUTTER = 0x2, MSG_FOCUS = 0x4, MSG_PREVIEW_FRAME = 0x10, MSG_VIDEO_FRAME = 0x20, MSG_RAW_IMAGE = 0x80, MSG_COMPRESSED_IMAGE = 0x100 };
const int INVALID_OPERATION = -38;
/* window.h: perform() codes and values of 2.3-4.1 */
enum { NWIN_SET_USAGE = 0, NWIN_SET_BUFFERS_GEOMETRY = 5, FMT_RGBA_8888 = 1, USAGE_SW_WRITE_OFTEN = 0x30, USAGE_HW_TEXTURE = 0x100 };

/* one size: the host's 640x480 frames */
const char DEFAULTS[] =
    "preview-size=640x480;preview-size-values=640x480;preview-format=yuv420sp;preview-format-values=yuv420sp;"
    "preview-frame-rate=30;preview-frame-rate-values=15,30;preview-fps-range=15000,30000;"
    "preview-fps-range-values=(15000,30000);picture-size=640x480;picture-size-values=640x480;"
    "picture-format=jpeg;picture-format-values=jpeg;jpeg-quality=92;jpeg-thumbnail-width=0;jpeg-thumbnail-height=0;"
    "jpeg-thumbnail-size-values=0x0;jpeg-thumbnail-quality=80;focus-mode=auto;focus-mode-values=auto,infinity;"
    "whitebalance=auto;whitebalance-values=auto;effect=none;effect-values=none;antibanding=auto;"
    "antibanding-values=auto;flash-mode=off;flash-mode-values=off;scene-mode=auto;scene-mode-values=auto;"
    "zoom-supported=true;zoom=0;max-zoom=10;zoom-ratios=100,120,140,160,180,200,220,240,260,280,300;"
    "smooth-zoom-supported=false;exposure-compensation=0;max-exposure-compensation=0;"
    "min-exposure-compensation=0;exposure-compensation-step=0;focal-length=3.5;horizontal-view-angle=54.8;"
    "vertical-view-angle=42.5;video-frame-format=yuv420sp;video-size=640x480;video-size-values=640x480;"
    "preferred-preview-size-for-video=640x480";

struct image_rect { uint32_t width, height; };
image_rect g_rect = { W, H };

const gr_module *gralloc() {
    static const gr_module *m;
    if (!m) {
        void *h = dlopen("libhardware.so", 0);
        int (*get)(const char *, const void **) = h ? (int (*)(const char *, const void **))dlsym(h, "hw_get_module") : 0;
        const void *g = 0;
        if (get && get("gralloc", &g) == 0) m = (const gr_module *)g;
        if (!m) LOG("no gralloc module: no preview");
    }
    return m;
}

sp<IMemoryHeap> newHeap(size_t size, const char *name, MemoryHeapBase **base) {
    MemoryHeapBase *h = new (operator new(256)) MemoryHeapBase(size, 0, name);
    *base = h;
    return sp<IMemoryHeap>(h);
}

sp<IMemory> newMem(const sp<IMemoryHeap> &heap, long off, size_t size) {
    return sp<IMemory>(new (operator new(128)) MemoryBase(heap, off, size));
}

class AemuCamera : public CameraHardwareInterface {
public:
    AemuCamera() : fd_(-1), msgs_(0), previewing_(false), recording_(false), zoom_(0), sent_(0), thread_(0), next_(0),
                   notify_(0), data_(0), dataTs_(0), user_(0), win_(0), winSet_(false), fails_(0) {
        memset(lock_, 0, sizeof lock_);
        memset((void *)busy_, 0, sizeof busy_);
        size_t n = sizeof DEFAULTS;
        params_ = (char *)malloc(n);
        memcpy(params_, DEFAULTS, n);
        MemoryHeapBase *b;
        previewHeap_ = newHeap(NV21_SIZE * NBUF, "aemu-preview", &b);
        preview_ = (uint8_t *)b->getBase();
        for (int i = 0; i < NBUF; i++) buf_[i] = newMem(previewHeap_, i * NV21_SIZE, NV21_SIZE);
        rawHeap_ = newHeap(NV21_SIZE, "aemu-raw", &b);
        raw_ = (uint8_t *)b->getBase();
        rawMem_ = newMem(rawHeap_, 0, NV21_SIZE);
        rgba_ = (uint8_t *)malloc(RGBA_SIZE);

        fd_ = socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0);
        struct { uint16_t family; char path[108]; } a;
        memset(&a, 0, sizeof a);
        a.family = 1;
        memcpy(a.path, "/dev/socket/aemu_cam", 21);
        if (fd_ >= 0 && connect(fd_, &a, sizeof a) != 0) { close(fd_); fd_ = -1; }
        if (fd_ < 0) { LOG("no /dev/socket/aemu_cam"); return; }
        say("open 0\n");
        incStrong(this);   /* the reader's reference, dropped when release() ends it */
        if (pthread_create(&thread_, 0, reader, this) != 0) thread_ = 0;
    }
    ~AemuCamera() { free(params_); free(rgba_); }

    int setPreviewWindow(const sp<ANativeWindow> &w) {
        pthread_mutex_lock(lock_);
        ANativeWindow *old = win_;
        win_ = w.get();
        if (win_) win_->incStrong(this);
        winSet_ = false;
        pthread_mutex_unlock(lock_);
        if (old) old->decStrong(this);
        return 0;
    }
    sp<IMemoryHeap> getRawHeap() const { return rawHeap_; }
    void setCallbacks(notify_callback n, data_callback d, data_callback_timestamp t, void *u) {
        notify_ = n; data_ = d; dataTs_ = t; user_ = u;
    }
    void enableMsgType(int32_t m) { msgs_ |= m; }
    void disableMsgType(int32_t m) { msgs_ &= ~m; }
    bool msgTypeEnabled(int32_t m) { return (msgs_ & m) != 0; }
    int startPreview() { previewing_ = true; say("start\n"); return 0; }
    void stopPreview() { previewing_ = false; say("stop\n"); }
    bool previewEnabled() { return previewing_; }
    int getNumberOfVideoBuffers() const { return 0; }
    sp<IMemory> getVideoBuffer(int) const { return sp<IMemory>(); }
    int storeMetaDataInBuffers(bool on) { return on ? INVALID_OPERATION : 0; }
    int startRecording() {
        if (fd_ < 0) return INVALID_OPERATION;
        recording_ = true; sent_ = 0;
        return 0;
    }
    void stopRecording() { recording_ = false; LOG("recording stopped after %d frames", sent_); }
    bool recordingEnabled() { return recording_; }
    void releaseRecordingFrame(const sp<IMemory> &m) {
        for (int i = 0; i < NBUF; i++) if (buf_[i].get() == m.get()) busy_[i] = 0;
    }
    int autoFocus() {
        /* the answer comes from another thread: Client::notifyCallback waits for the lock autoFocus holds */
        long t;
        incStrong(this);
        if (pthread_create(&t, 0, focused, this) != 0) { decStrong(this); return INVALID_OPERATION; }
        pthread_detach(t);
        return 0;
    }
    int cancelAutoFocus() { return 0; }
    int takePicture() {
        if (fd_ < 0) return INVALID_OPERATION;
        previewing_ = false;   /* the host stops the preview for the picture; the application starts it again */
        say("shot\n");
        return 0;
    }
    int cancelPicture() { return 0; }
    int setParameters(const CameraParameters &p) {
        String8 s = p.flatten();
        size_t n = strlen(s.string()) + 1;
        char *c = (char *)malloc(n);
        if (!c) return -12;
        memcpy(c, s.string(), n);
        pthread_mutex_lock(lock_);
        char *old = params_; params_ = c;
        pthread_mutex_unlock(lock_);
        free(old);
        /* "zoom" is an index into zoom-ratios (CameraParameters.h): 100 + 20 per step; the host crops the frame */
        int z = zoomOf(c);
        if (z != zoom_) { zoom_ = z; char m[32]; snprintf(m, sizeof m, "zoom %d\n", 100 + 20 * z); say(m); }
        return 0;
    }
    CameraParameters getParameters() const {
        CameraParameters p;
        pthread_mutex_lock((void *)lock_);
        p.unflatten(String8(params_));
        pthread_mutex_unlock((void *)lock_);
        p.set("preview-size", "640x480");
        p.set("video-size", "640x480");
        p.set("picture-size", "640x480");
        return p;
    }
    int sendCommand(int32_t, int32_t, int32_t) { return 0; }
    void release() {
        previewing_ = false;
        if (fd_ >= 0) { say("close\n"); shutdown(fd_, 2); }
        if (thread_) { pthread_join(thread_, 0); thread_ = 0; }
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
        setPreviewWindow(sp<ANativeWindow>());
    }
    int dump(int, const void *) const { return 0; }

private:
    void say(const char *s) {
        if (fd_ < 0) return;
        unsigned long n = strlen(s);
        while (n) { long r = write(fd_, s, n); if (r <= 0) return; s += r; n -= r; }
    }
    static int zoomOf(const char *p) {
        for (const char *q = p; *q; q++)
            if ((q == p || q[-1] == ';') && q[0] == 'z' && q[1] == 'o' && q[2] == 'o' && q[3] == 'm' && q[4] == '=') {
                int z = atoi(q + 5);
                return z < 0 ? 0 : z > 10 ? 10 : z;
            }
        return 0;
    }
    bool readAll(void *p, unsigned long n) {
        uint8_t *d = (uint8_t *)p;
        while (n) { long r = read(fd_, d, n); if (r <= 0) return false; d += r; n -= r; }
        return true;
    }
    static void *focused(void *self) {
        AemuCamera *c = (AemuCamera *)self;
        if (c->notify_ && (c->msgs_ & MSG_FOCUS)) c->notify_(MSG_FOCUS, 1, 0, c->user_);
        c->decStrong(c);
        return 0;
    }
    static void *reader(void *self) {
        AemuCamera *c = (AemuCamera *)self;
        c->loop();
        c->decStrong(c);
        return 0;
    }
    /* the RGBA frame into the next buffer of the window */
    void show() {
        pthread_mutex_lock(lock_);
        ANativeWindow *w = win_;
        if (w) w->incStrong(this);
        bool fresh = !winSet_; winSet_ = true;
        pthread_mutex_unlock(lock_);
        const gr_module *g = gralloc();
        if (!w || !g) { if (w) w->decStrong(this); return; }
        if (fresh) {
            w->perform(w, NWIN_SET_USAGE, USAGE_SW_WRITE_OFTEN | USAGE_HW_TEXTURE);
            w->perform(w, NWIN_SET_BUFFERS_GEOMETRY, W, H, FMT_RGBA_8888);
        }
        ANativeWindowBuffer *b = 0;
        int r = w->dequeueBuffer(w, &b);
        if (r == 0 && b) {
            if (w->lockBuffer) w->lockBuffer(w, b);
            void *va = 0;
            if (g->lock(g, b->handle, USAGE_SW_WRITE_OFTEN, 0, 0, b->width, b->height, &va) == 0 && va) {
                int rows = b->height < H ? b->height : H, cols = b->width < W ? b->width : W;
                for (int y = 0; y < rows; y++) memcpy((uint8_t *)va + y * b->stride * 4, rgba_ + y * W * 4, cols * 4);
                g->unlock(g, b->handle);
                w->queueBuffer(w, b);
            } else if (w->cancelBuffer) w->cancelBuffer(w, b);
            else w->queueBuffer(w, b);
        } else if (++fails_ <= 3) LOG("preview window: dequeueBuffer %d", r);
        w->decStrong(this);
    }
    void loop() {
        int32_t h[4];
        uint8_t *jpg = 0; size_t jcap = 0;
        while (readAll(h, sizeof h)) {
            uint32_t len = (uint32_t)h[3];
            if (len > (32u << 20)) break;
            if (h[0] == 1 && h[1] == W && h[2] == H && len == (uint32_t)(RGBA_SIZE + NV21_SIZE)) {
                if (!readAll(rgba_, RGBA_SIZE)) break;
                int i = -1;
                for (int k = 0; k < NBUF; k++) { int j = (next_ + k) % NBUF; if (!busy_[j]) { i = j; break; } }
                if (!readAll(i >= 0 ? preview_ + i * NV21_SIZE : raw_, NV21_SIZE)) break;
                if (!previewing_) continue;
                show();
                if (i < 0) continue;
                next_ = (i + 1) % NBUF;
                if (data_ && (msgs_ & MSG_PREVIEW_FRAME)) data_(MSG_PREVIEW_FRAME, buf_[i], user_);
                if (recording_ && dataTs_ && (msgs_ & MSG_VIDEO_FRAME)) {
                    struct { long s, ns; } t;
                    clock_gettime(1 /* CLOCK_MONOTONIC: systemTime() */, &t);
                    busy_[i] = 1;
                    dataTs_((int64_t)t.s * 1000000000LL + t.ns, MSG_VIDEO_FRAME, buf_[i], user_);
                    sent_++;
                }
            } else if (h[0] == 2) {
                if (len > jcap) { free(jpg); jpg = (uint8_t *)malloc(len); jcap = jpg ? len : 0; if (!jpg) break; }
                if (!readAll(jpg, len)) break;
                picture(jpg, len);
            } else {   /* anything else: skip it */
                uint8_t tmp[4096];
                while (len) { uint32_t k = len < sizeof tmp ? len : sizeof tmp; if (!readAll(tmp, k)) { len = 1; break; } len -= k; }
                if (len) break;
            }
        }
        free(jpg);
    }
    void picture(const uint8_t *jpg, uint32_t len) {
        if (notify_ && (msgs_ & MSG_SHUTTER)) notify_(MSG_SHUTTER, (int32_t)(intptr_t)&g_rect, 0, user_);
        if (data_ && (msgs_ & MSG_RAW_IMAGE)) {
            memcpy(raw_, preview_ + ((next_ + NBUF - 1) % NBUF) * NV21_SIZE, NV21_SIZE);
            data_(MSG_RAW_IMAGE, rawMem_, user_);
        }
        if (data_ && (msgs_ & MSG_COMPRESSED_IMAGE)) {
            MemoryHeapBase *b;
            sp<IMemoryHeap> heap = newHeap(len, "aemu-jpeg", &b);
            memcpy(b->getBase(), jpg, len);
            sp<IMemory> mem = newMem(heap, 0, len);
            data_(MSG_COMPRESSED_IMAGE, mem, user_);
        }
        LOG("picture: %u bytes of JPEG", len);
    }

    int fd_;
    volatile int32_t msgs_;
    volatile bool previewing_, recording_;
    int zoom_, sent_;
    volatile int busy_[NBUF];
    long thread_;
    int next_;
    notify_callback notify_;
    data_callback data_;
    data_callback_timestamp dataTs_;
    void *user_;
    ANativeWindow *win_;
    bool winSet_;
    int fails_;
    char *params_;
    long lock_[4];   /* pthread_mutex_t of bionic: one int, zero = unlocked */
    sp<IMemoryHeap> previewHeap_, rawHeap_;
    sp<IMemory> buf_[NBUF], rawMem_;
    uint8_t *preview_, *raw_, *rgba_;
};

}  // namespace

struct CameraInfo { int facing, orientation; };

extern "C" EXPORT int HAL_getNumberOfCameras() { return 1; }
extern "C" EXPORT void HAL_getCameraInfo(int, CameraInfo *info) {
    if (info) { info->facing = 0; info->orientation = 90; }   /* a phone's back sensor: landscape, turned 90 */
}
/* sp<CameraHardwareInterface> HAL_openCameraHardware(int): the caller passes the place for the result */
extern "C" EXPORT void HAL_openCameraHardware(sp<CameraHardwareInterface> *result, int) {
    new (result) sp<CameraHardwareInterface>(new AemuCamera());
}
extern "C" EXPORT int SEC_getNumberOfCameras() { return HAL_getNumberOfCameras(); }
extern "C" EXPORT void SEC_getCameraInfo(int i, CameraInfo *info) { HAL_getCameraInfo(i, info); }
extern "C" EXPORT void SEC_openCameraHardware(sp<CameraHardwareInterface> *result, int i) { HAL_openCameraHardware(result, i); }
