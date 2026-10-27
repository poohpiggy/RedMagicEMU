/*
 * libcamera.so for Android 2.3 firmwares whose CameraService links the camera HAL directly (HTC): the phone's back
 * camera through the emulator's /dev/socket/aemu_cam (CameraFeed).
 *
 * Interface: CameraHardwareInterface of 2.3 (frameworks/base/include/camera/CameraHardwareInterface.h), with the
 * slot order of HTC's CameraService checked against its libcameraservice.so (Desire HD 2.3.5):
 *   Client::Client: +0x10 setCallbacks, +0x2c useOverlay; startPreviewMode: +0x20 startPreview, +0x38 previewEnabled;
 *   registerPreviewBuffers: +0x08 getPreviewHeap, +0x60 getParameters; setOverlay: +0x30 setOverlay(sp<Overlay>).
 *   HTC adds getBufferInfo / encodeData after startPreview, a method after getParameters and 13 after dump.
 * The preview goes through an overlay (Client always formats non-overlay buffers as YCrCb_420_SP for SurfaceFlinger):
 * the HAL tells the host which overlay ("ovl <id>", from the overlay's handle, libui's Overlay: +4 OverlayRef,
 * OverlayRef +8 handle; our overlay HAL puts the id in data[0]) and the host draws the frames into it. The guest only
 * gets NV21 for the application's preview callbacks and the JPEG of a picture.
 *
 * Classes are declared here with the inheritance of the 2.3 headers so that the compiler finds the virtual bases the
 * same way the firmware does (Itanium C++ ABI, as GCC 4.4 of the firmware); objects of the firmware's classes are
 * only made by their exported constructors. No C++ runtime of our own: operator new/delete and __cxa_pure_virtual
 * come from the firmware's libstdc++.so.
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
class Overlay;

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

/* the slots of HTC's CameraService, in order (see the top) */
class CameraHardwareInterface : public virtual RefBase {
public:
    virtual ~CameraHardwareInterface() {}
    virtual sp<IMemoryHeap> getPreviewHeap() const = 0;                       /* 0x08 */
    virtual sp<IMemoryHeap> getRawHeap() const = 0;
    virtual void setCallbacks(notify_callback, data_callback, data_callback_timestamp, void *) = 0;   /* 0x10 */
    virtual void enableMsgType(int32_t) = 0;
    virtual void disableMsgType(int32_t) = 0;
    virtual bool msgTypeEnabled(int32_t) = 0;
    virtual int startPreview() = 0;                                           /* 0x20 */
    virtual int getBufferInfo(sp<IMemory> &mem, uint32_t *size) = 0;          /* HTC */
    virtual int encodeData() = 0;                                             /* HTC */
    virtual bool useOverlay() = 0;                                            /* 0x2c */
    virtual int setOverlay(const sp<Overlay> &) = 0;                          /* 0x30 */
    virtual void stopPreview() = 0;
    virtual bool previewEnabled() = 0;                                        /* 0x38 */
    virtual int startRecording() = 0;
    virtual void stopRecording() = 0;
    virtual bool recordingEnabled() = 0;
    virtual void releaseRecordingFrame(const sp<IMemory> &) = 0;
    virtual int autoFocus() = 0;
    virtual int cancelAutoFocus() = 0;
    virtual int takePicture() = 0;
    virtual int cancelPicture() = 0;
    virtual int setParameters(const CameraParameters &) = 0;
    virtual CameraParameters getParameters() const = 0;                       /* 0x60 */
    virtual int htc25(int) = 0;
    virtual int sendCommand(int32_t, int32_t, int32_t) = 0;
    virtual void release() = 0;
    virtual int dump(int fd, const void *args) const = 0;
    virtual int htc29(int) = 0; virtual int htc30(int) = 0; virtual int htc31(int) = 0; virtual int htc32(int) = 0;
    virtual int htc33(int) = 0; virtual int htc34(int) = 0; virtual int htc35(int) = 0; virtual int htc36(int) = 0;
    virtual int htc37(int) = 0; virtual int htc38(int) = 0; virtual int htc39(int) = 0; virtual int htc40(int) = 0;
    virtual int htc41(int) = 0;
};

}  // namespace android

using namespace android;

namespace {

enum { W = 640, H = 480, NV21_SIZE = W * H * 3 / 2, NBUF = 6 };
/* the largest preview the host is asked for: buffers are this big, each frame uses its own size */
enum { MAXW = 800, MAXH = 480, MAX_SIZE = MAXW * MAXH * 3 / 2 };
/* sizes offered for preview and video; the host crops its 4:3 frame to the shape and scales it */
const char *const SIZES[] = { "800x480", "720x480", "640x480", "352x288", "320x240", "176x144", 0 };
enum { MSG_SHUTTER = 0x2, MSG_FOCUS = 0x4, MSG_PREVIEW_FRAME = 0x10, MSG_VIDEO_FRAME = 0x20,   /* camera/Camera.h 2.3; HTC startRecordingMode enables 0x20 */ MSG_RAW_IMAGE = 0x80, MSG_COMPRESSED_IMAGE = 0x100 };
const int INVALID_OPERATION = -38;

/* what an application of 2.3 expects to find (CameraParameters.h keys); one size: the host's 640x480 frames */
const char DEFAULTS[] =
    "preview-size=640x480;preview-size-values=800x480,720x480,640x480,352x288,320x240,176x144;preview-format=yuv420sp;preview-format-values=yuv420sp;"
    "preview-frame-rate=30;preview-frame-rate-values=15,30;preview-fps-range=15000,30000;"
    "preview-fps-range-values=(15000,30000);picture-size=640x480;picture-size-values=640x480;"
    "picture-format=jpeg;picture-format-values=jpeg;jpeg-quality=92;jpeg-thumbnail-width=0;jpeg-thumbnail-height=0;"
    "jpeg-thumbnail-size-values=0x0;jpeg-thumbnail-quality=80;focus-mode=auto;focus-mode-values=auto,infinity;"
    "whitebalance=auto;whitebalance-values=auto;effect=none;effect-values=none;antibanding=auto;"
    "antibanding-values=auto;flash-mode=off;flash-mode-values=off;scene-mode=auto;scene-mode-values=auto;"
    "zoom-supported=true;zoom=0;max-zoom=10;zoom-ratios=100,120,140,160,180,200,220,240,260,280,300;"
    "smooth-zoom-supported=false;exposure-compensation=0;max-exposure-compensation=0;"
    "min-exposure-compensation=0;exposure-compensation-step=0;focal-length=3.5;horizontal-view-angle=54.8;"
    "vertical-view-angle=42.5;video-frame-format=yuv420sp;video-size=640x480;video-size-values=800x480,720x480,640x480,352x288,320x240,176x144;"
    "preferred-preview-size-for-video=640x480";

struct image_rect { uint32_t width, height; };
image_rect g_rect = { W, H };

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
    AemuCamera() : fd_(-1), msgs_(0), previewing_(false), recording_(false), zoom_(0), pw_(W), ph_(H), bufSize_(0), sent_(0), overlay_(0), thread_(0), next_(0),
                   notify_(0), data_(0), dataTs_(0), user_(0) {
        memset(lock_, 0, sizeof lock_);
        memset((void *)busy_, 0, sizeof busy_);
        size_t n = sizeof DEFAULTS;
        params_ = (char *)malloc(n);
        memcpy(params_, DEFAULTS, n);
        MemoryHeapBase *b;
        previewHeap_ = newHeap(MAX_SIZE * NBUF, "aemu-preview", &b);
        preview_ = (uint8_t *)b->getBase();
        for (int i = 0; i < NBUF; i++) buf_[i] = newMem(previewHeap_, i * MAX_SIZE, NV21_SIZE);
        bufSize_ = NV21_SIZE;
        rawHeap_ = newHeap(NV21_SIZE, "aemu-raw", &b);
        raw_ = (uint8_t *)b->getBase();
        rawMem_ = newMem(rawHeap_, 0, NV21_SIZE);

        fd_ = socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0);
        struct { uint16_t family; char path[108]; } a;
        memset(&a, 0, sizeof a);
        a.family = 1;
        memcpy(a.path, "/dev/socket/aemu_cam", 21);
        if (fd_ >= 0 && connect(fd_, &a, sizeof a) != 0) { close(fd_); fd_ = -1; }
        if (fd_ < 0) { LOG("no /dev/socket/aemu_cam"); return; }
        say("open 0\nnv21\n");
        /* the reader holds the camera until release() ends it (a failed start keeps that one reference: dropping
         * it here, before the caller's sp exists, would delete the object inside its constructor) */
        incStrong(this);
        if (pthread_create(&thread_, 0, reader, this) != 0) thread_ = 0;
    }
    ~AemuCamera() { free(params_); }

    sp<IMemoryHeap> getPreviewHeap() const { return previewHeap_; }
    sp<IMemoryHeap> getRawHeap() const { return rawHeap_; }
    void setCallbacks(notify_callback n, data_callback d, data_callback_timestamp t, void *u) {
        notify_ = n; data_ = d; dataTs_ = t; user_ = u;
    }
    void enableMsgType(int32_t m) { msgs_ |= m; }
    void disableMsgType(int32_t m) { msgs_ &= ~m; }
    bool msgTypeEnabled(int32_t m) { return (msgs_ & m) != 0; }
    int startPreview() {
        previewing_ = true;
        sayOverlay();
        say("start\n");
        return 0;
    }
    int getBufferInfo(sp<IMemory> &, uint32_t *) { return INVALID_OPERATION; }
    int encodeData() { return 0; }
    bool useOverlay() { return true; }
    int setOverlay(const sp<Overlay> &o) {
        int id = 0;
        if (Overlay *p = o.get()) {
            void *ref = ((void **)p)[1];
            const int *h = ref ? ((const int **)ref)[2] : 0;   /* native_handle: version, numFds, numInts, data */
            if (h && h[2] >= 1) id = h[3 + h[1]];
        }
        overlay_ = id;
        if (previewing_) sayOverlay();
        return 0;
    }
    void stopPreview() { previewing_ = false; say("stop\n"); }
    bool previewEnabled() { return previewing_; }
    /* CameraSource (2.3): frames come with dataCallbackTimestamp(CAMERA_MSG_VIDEO_FRAME); each buffer stays the
     * recorder's until releaseRecordingFrame gives it back, so the reader never writes into it meanwhile */
    int startRecording() {
        if (fd_ < 0) return INVALID_OPERATION;
        recording_ = true; sent_ = 0;
        LOG("recording %dx%d, messages 0x%x, callback %p", pw_, ph_, (int)msgs_, (void *)dataTs_);
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
        int w, h;
        if (sizeOf(c, "preview-size=", &w, &h) && (w != pw_ || h != ph_)) {
            pw_ = w; ph_ = h;
            char m[40]; snprintf(m, sizeof m, "size %d %d\n", w, h); say(m);
        }
        return 0;
    }
    CameraParameters getParameters() const {
        CameraParameters p;
        pthread_mutex_lock((void *)lock_);
        p.unflatten(String8(params_));
        pthread_mutex_unlock((void *)lock_);
        /* preview and video: the size the host was asked for; pictures are the host's own frame */
        char sz[24];
        snprintf(sz, sizeof sz, "%dx%d", pw_, ph_);
        p.set("preview-size", sz);
        p.set("video-size", sz);
        p.set("picture-size", "640x480");
        return p;
    }
    int htc25(int) { return 0; }
    int sendCommand(int32_t, int32_t, int32_t) { return 0; }
    void release() {
        previewing_ = false;
        if (fd_ >= 0) { say("close\n"); shutdown(fd_, 2); }
        if (thread_) { pthread_join(thread_, 0); thread_ = 0; }
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
    }
    int dump(int, const void *) const { return 0; }
    int htc29(int) { return 0; } int htc30(int) { return 0; } int htc31(int) { return 0; } int htc32(int) { return 0; }
    int htc33(int) { return 0; } int htc34(int) { return 0; } int htc35(int) { return 0; } int htc36(int) { return 0; }
    int htc37(int) { return 0; } int htc38(int) { return 0; } int htc39(int) { return 0; } int htc40(int) { return 0; }
    int htc41(int) { return 0; }

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
    /* "<key>WxH" when that size is one of SIZES */
    static bool sizeOf(const char *p, const char *key, int *w, int *h) {
        unsigned long kl = strlen(key);
        for (const char *q = p; *q; q++) {
            if (q != p && q[-1] != ';') continue;
            unsigned long k = 0; while (k < kl && q[k] == key[k]) k++;
            if (k != kl) continue;
            const char *v = q + kl;
            for (int i = 0; SIZES[i]; i++) {
                unsigned long n = strlen(SIZES[i]); unsigned long j = 0;
                while (j < n && v[j] == SIZES[i][j]) j++;
                if (j == n && (v[n] == ';' || v[n] == 0)) { *w = atoi(v); const char *x = v; while (*x != 'x') x++; *h = atoi(x + 1); return true; }
            }
            return false;
        }
        return false;
    }
    void sayOverlay() {
        char m[32];
        snprintf(m, sizeof m, "ovl %d\n", overlay_);
        say(m);
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
    void loop() {
        int32_t h[4];
        uint8_t *jpg = 0; size_t jcap = 0;
        while (readAll(h, sizeof h)) {
            uint32_t len = (uint32_t)h[3];
            if (len > (32u << 20)) break;
            int i = -1;
            if (h[0] == 3 && h[1] > 0 && h[2] > 0 && h[1] <= MAXW && h[2] <= MAXH && len == (uint32_t)(h[1] * h[2] * 3 / 2)) {
                /* a new size: the buffers' memory objects describe it (the reader is the only one handing them out) */
                if ((int)len != bufSize_) {
                    for (int k = 0; k < NBUF; k++) { buf_[k] = newMem(previewHeap_, k * MAX_SIZE, len); busy_[k] = 0; }
                    bufSize_ = (int)len;
                }
                for (int k = 0; k < NBUF; k++) { int j = (next_ + k) % NBUF; if (!busy_[j]) { i = j; break; } }
            }
            if (i >= 0) {
                next_ = (i + 1) % NBUF;
                if (!readAll(preview_ + i * MAX_SIZE, len)) break;
                if (previewing_ && data_ && (msgs_ & MSG_PREVIEW_FRAME)) data_(MSG_PREVIEW_FRAME, buf_[i], user_);
                if (recording_ && dataTs_ && (msgs_ & MSG_VIDEO_FRAME)) {
                    struct { long s, ns; } t;
                    clock_gettime(1 /* CLOCK_MONOTONIC: systemTime() */, &t);
                    busy_[i] = 1;
                    dataTs_((int64_t)t.s * 1000000000LL + t.ns, MSG_VIDEO_FRAME, buf_[i], user_);
                    if (++sent_ == 1) LOG("first video frame, %u bytes", len);
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
            memcpy(raw_, preview_ + ((next_ + NBUF - 1) % NBUF) * MAX_SIZE, bufSize_ < NV21_SIZE ? bufSize_ : NV21_SIZE);
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
    int zoom_, pw_, ph_, bufSize_, sent_;
    volatile int busy_[NBUF];
    int overlay_;
    long thread_;
    int next_;
    notify_callback notify_;
    data_callback data_;
    data_callback_timestamp dataTs_;
    void *user_;
    char *params_;
    long lock_[4];   /* pthread_mutex_t of bionic: one int, zero = unlocked */
    sp<IMemoryHeap> previewHeap_, rawHeap_;
    sp<IMemory> buf_[NBUF], rawMem_;
    uint8_t *preview_, *raw_;
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
