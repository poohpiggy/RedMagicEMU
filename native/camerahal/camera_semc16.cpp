/*
 * libcamera.so for Android 1.6 firmwares with Sony Ericsson's CameraService (X10): the phone's back camera through
 * the emulator's /dev/socket/aemu_cam (CameraFeed), in place of QualcommCameraHardware, which drives /dev/msm_camera.
 *
 * Interface: CameraHardwareInterface of 1.6 (callbacks given to startPreview/takePicture/autoFocus, no setCallbacks)
 * with SEMC's additions. Slot order from the firmware's own vtable of QualcommCameraHardware (libcamera.so,
 * _ZTVN7android22QualcommCameraHardwareE, prelinked at 0x69680000), and the calls of its libcameraservice.so:
 *   registerPreviewBuffers: getParameters +0x4c, getPreviewHeapnew(0..3) +0xc, four heaps given to the surface as
 *   YCrCb_420_SP; startPreviewMode: useOverlay +0x18, startPreview +0x14, previewEnabled +0x28; takePicture +0x40;
 *   previewCallback(mem, index, user) -> postPreviewFrame: mem->getMemory, ISurface::postBuffer(index);
 *   then getFaceDetectMode +0x98 (0: no face detection).
 * Each of the four preview heaps holds one frame; the index given with a frame is its heap.
 * Classes follow the 1.6 headers (binder classes in libutils, CameraParameters in libui); objects of the firmware's
 * classes are only made by their exported constructors, operator new/delete come from its libstdc++.so.
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

typedef void (*preview_callback)(const sp<IMemory> &mem, int index, void *user);
typedef void (*zoom_callback)(void *, void *, void *);
typedef void (*recording_callback)(int64_t ts, const sp<IMemory> &mem, void *user);
typedef void (*autofocus_callback)(bool focused, void *user);
typedef void (*shutter_callback)(void *user);
typedef void (*picture_callback)(const sp<IMemory> &mem, void *user);

/* the slots of SEMC's QualcommCameraHardware, in order (offsets from the vtable pointer) */
class CameraHardwareInterface : public virtual RefBase {
public:
    virtual ~CameraHardwareInterface() {}                                                     /* 0x00, 0x04 */
    virtual sp<IMemoryHeap> getPreviewHeap() const = 0;                                       /* 0x08 */
    virtual sp<IMemoryHeap> getPreviewHeapnew(int) const = 0;                                 /* 0x0c */
    virtual sp<IMemoryHeap> getRawHeap() const = 0;                                           /* 0x10 */
    virtual int startPreview(preview_callback, zoom_callback, void *) = 0;                    /* 0x14 */
    virtual bool useOverlay() = 0;                                                            /* 0x18 */
    virtual int setOverlay(const sp<Overlay> &) = 0;                                          /* 0x1c */
    virtual void stopPreview() = 0;                                                           /* 0x20 */
    virtual void forceCloseStopPreview() = 0;                                                 /* 0x24 */
    virtual bool previewEnabled() = 0;                                                        /* 0x28 */
    virtual int startRecording(recording_callback, void *) = 0;                               /* 0x2c */
    virtual void stopRecording() = 0;
    virtual bool recordingEnabled() = 0;
    virtual void releaseRecordingFrame(const sp<IMemory> &) = 0;
    virtual int autoFocus(autofocus_callback, void *) = 0;                                    /* 0x3c */
    virtual int takePicture(shutter_callback, picture_callback, picture_callback, void *) = 0;   /* 0x40 */
    virtual int cancelPicture(bool, bool, bool) = 0;
    virtual int setParameters(const CameraParameters &) = 0;                                  /* 0x48 */
    virtual CameraParameters getParameters() const = 0;                                       /* 0x4c */
    virtual void release() = 0;                                                               /* 0x50 */
    virtual int dump(int fd, const void *args) const = 0;
    virtual int semcCameraExtensionService() = 0;                                             /* 0x58 */
    virtual int setParametersInt(CameraParameters &, const char *, int) = 0;
    virtual int setParametersStr(CameraParameters &, const char *, const char *) = 0;
    virtual int setPreviewSize(CameraParameters &, int, int) = 0;
    virtual int setPictureSize(CameraParameters &, int, int) = 0;
    virtual int getExifInfo() = 0;
    virtual int startAutoFocus(void (*)(int, int, int, void *), void *, bool, bool, bool) = 0;   /* 0x70 */
    virtual int stopAutoFocus() = 0;
    virtual int startAutoZoom(int, int, void (*)(int, int, void *), void *) = 0;
    virtual int stopAutoZoom() = 0;
    virtual int setZoom(int) = 0;                                                             /* 0x80 */
    virtual int getMaxZoom(int, int) = 0;
    virtual int setAeAfPosition(int, int, int, int, int) = 0;   /* (setAeAfPosition_t, int): arguments unused */
    virtual int getFocusPosition() = 0;
    virtual int getStatus() = 0;                                                              /* 0x90 */
    virtual int getPreviewSize(int *, int *) = 0;
    virtual int getFaceDetectMode() = 0;                                                      /* 0x98 */
    virtual int getSceneNvValue(void *, int) = 0;
    virtual int setSceneRecognitionCallback(void *, void *) = 0;
    virtual int setAbstractCallback(void (*)(int, void *), void *) = 0;
    virtual int setSceneRecognitionInfo(void *, int) = 0;
    virtual int setCollective() = 0;
    virtual int convertImage864to854(unsigned char *, unsigned char *) = 0;
    virtual int convertImage864to848(unsigned char *, unsigned char *) = 0;                  /* 0xb4 */
};

}  // namespace android

using namespace android;

namespace {

enum { W = 640, H = 480, NV21_SIZE = W * H * 3 / 2, NHEAP = 4 };
/* the raw picture SEMC's stack hands on: JNICameraContext::postData (libandroid_runtime 0x5374e) makes an int[] of
 * size/4 from it (ARGB_8888 pixels), which SemcCameraApplication's CameraController.doGetRawData turns into a
 * Bitmap of getThumbnailWidth x getThumbnailHeight: 640x480 for 4:3 photos, 854x480 for 16:9
 * (CameraConfigManager.<clinit>, sThumbnailSizeMap) */
enum { RAWW_MAX = 854, RAWH = 480, RAW_SIZE = RAWW_MAX * RAWH * 4 };
/* the buffers CameraService and the application see are YUYV (preview-format yuv422i: the formats its setOverlay
 * takes are yuv422i and rgb565); the host sends NV21, converted here */
enum { MAXW = 800, MAXH = 480, MAX_SIZE = MAXW * MAXH * 2, YUYV_SIZE = W * H * 2 };
/* sizes offered for preview and video; the host crops its 4:3 frame to the shape and scales it (no 848/852: the
 * firmware's CameraService pads those to 864, which only its own hardware delivers) */
const char *const SIZES[] = { "800x480", "720x480", "640x480", "352x288", "320x240", "176x144", 0 };
const int INVALID_OPERATION = -38;

/* what an application of 1.6 expects to find (CameraParameters.h keys of 1.6) */
const char DEFAULTS[] =
    "preview-size=640x480;preview-size-values=800x480,720x480,640x480,352x288,320x240,176x144;preview-format=yuv422i;preview-format-values=yuv422i;"
    "preview-frame-rate=30;preview-frame-rate-values=15,30;picture-size=640x480;picture-size-values=640x480;"
    "picture-format=jpeg;picture-format-values=jpeg;jpeg-quality=92;jpeg-thumbnail-width=0;jpeg-thumbnail-height=0;"
    "jpeg-thumbnail-quality=80;focus-mode=auto;focus-mode-values=auto,infinity;whitebalance=auto;whitebalance-values=auto;"
    "effect=none;effect-values=none;antibanding=auto;antibanding-values=auto;flash-mode=off;flash-mode-values=off;"
    "scene-mode=auto;scene-mode-values=auto";

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
    AemuCamera() : fd_(-1), previewing_(false), recording_(false), pw_(W), ph_(H), bufSize_(0), sent_(0), overlay_(0), fw_(W), fh_(H), nv21_(0), thread_(0), next_(0),
                   preview_cb_(0), rec_cb_(0), af_cb_(0), shutter_cb_(0), raw_cb_(0), jpeg_cb_(0), user_(0), recUser_(0), afUser_(0), picUser_(0) {
        memset(lock_, 0, sizeof lock_);
        memset((void *)busy_, 0, sizeof busy_);
        size_t n = sizeof DEFAULTS;
        params_ = (char *)malloc(n);
        memcpy(params_, DEFAULTS, n);
        MemoryHeapBase *b;
        for (int i = 0; i < NHEAP; i++) {
            heap_[i] = newHeap(MAX_SIZE, "aemu-preview", &b);
            frame_[i] = (uint8_t *)b->getBase();
            buf_[i] = newMem(heap_[i], 0, YUYV_SIZE);
        }
        bufSize_ = YUYV_SIZE;
        nv21_ = (uint8_t *)malloc(MAXW * MAXH * 3 / 2);
        rawHeap_ = newHeap(RAW_SIZE, "aemu-raw", &b);
        raw_ = (uint8_t *)b->getBase();

        fd_ = socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0);
        struct { uint16_t family; char path[108]; } a;
        memset(&a, 0, sizeof a);
        a.family = 1;
        memcpy(a.path, "/dev/socket/aemu_cam", 21);
        if (fd_ >= 0 && connect(fd_, &a, sizeof a) != 0) { close(fd_); fd_ = -1; }
        if (fd_ < 0) { LOG("no /dev/socket/aemu_cam"); return; }
        /* 1.6 has no display orientation (Camera.setDisplayOrientation is API 8): the preview is shown upright, as
         * the phone's own HAL does; the frames the application gets stay as the sensor gives them */
        say("open 0\nnv21\nupright\n");
        /* the reader holds the camera until release() ends it */
        incStrong(this);
        if (pthread_create(&thread_, 0, reader, this) != 0) thread_ = 0;
    }
    ~AemuCamera() { free(params_); free(nv21_); }

    sp<IMemoryHeap> getPreviewHeap() const { return heap_[0]; }
    sp<IMemoryHeap> getPreviewHeapnew(int i) const { return heap_[i >= 0 && i < NHEAP ? i : 0]; }
    sp<IMemoryHeap> getRawHeap() const { return rawHeap_; }
    int startPreview(preview_callback cb, zoom_callback, void *user) {
        /* with an overlay, startPreviewMode passes no callback (startPreview(NULL, NULL, cookie)): the application's
         * preview callbacks would get nothing. The callback it passes without one, Client::previewCallback, also
         * hands the frame to the application; its ISurface::postBuffer goes to the overlay's source, which does
         * nothing with it (LayerBuffer::Source::postBuffer) */
        if (!cb) cb = (preview_callback)dlsym((void *)0xffffffff /* RTLD_DEFAULT of 1.x bionic */,
                                              "_ZN7android13CameraService6Client15previewCallbackERKNS_2spINS_7IMemoryEEEiPv");
        preview_cb_ = cb; user_ = user;
        previewing_ = true;
        sayOverlay();
        say("start\n");
        return 0;
    }
    /* the preview through an overlay (the host draws the frames in its rectangle, in colour): without one the
     * firmware's SurfaceFlinger shows YCrCb_420_SP buffers through pixelflinger, which reads the luma only */
    bool useOverlay() { return true; }
    int setOverlay(const sp<Overlay> &o) {
        int id = 0;
        if (Overlay *p = o.get()) {
            /* libui 1.6: Overlay +4 OverlayRef, OverlayRef +8 native_handle (Overlay::Overlay, OverlayRef::OverlayRef);
             * our overlay HAL puts the id in data[0] */
            void *ref = ((void **)p)[1];
            const int *h = ref ? ((const int **)ref)[2] : 0;
            if (h && h[2] >= 1) id = h[3 + h[1]];
        }
        overlay_ = id;
        if (previewing_) sayOverlay();
        return 0;
    }
    void stopPreview() { previewing_ = false; say("stop\n"); }
    void forceCloseStopPreview() { stopPreview(); }
    bool previewEnabled() { return previewing_; }
    int startRecording(recording_callback cb, void *user) {
        if (fd_ < 0) return INVALID_OPERATION;
        rec_cb_ = cb; recUser_ = user;
        recording_ = true; sent_ = 0;
        return 0;
    }
    void stopRecording() { recording_ = false; LOG("recording stopped after %d frames", sent_); }
    bool recordingEnabled() { return recording_; }
    void releaseRecordingFrame(const sp<IMemory> &m) {
        for (int i = 0; i < NHEAP; i++) if (buf_[i].get() == m.get()) busy_[i] = 0;
    }
    int autoFocus(autofocus_callback cb, void *user) {
        LOG("autoFocus");
        /* the answer from another thread: the caller holds its lock while it asks */
        af_cb_ = cb; afUser_ = user;
        long t;
        incStrong(this);
        if (pthread_create(&t, 0, focused, this) != 0) { decStrong(this); return INVALID_OPERATION; }
        pthread_detach(t);
        return 0;
    }
    int takePicture(shutter_callback s, picture_callback raw, picture_callback jpeg, void *user) {
        LOG("takePicture");
        if (fd_ < 0) return INVALID_OPERATION;
        shutter_cb_ = s; raw_cb_ = raw; jpeg_cb_ = jpeg; picUser_ = user;
        previewing_ = false;   /* the host stops the preview for the picture; the application starts it again */
        say("shot\n");
        return 0;
    }
    int cancelPicture(bool, bool, bool) { shutter_cb_ = 0; raw_cb_ = 0; jpeg_cb_ = 0; return 0; }
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
        char sz[24];
        snprintf(sz, sizeof sz, "%dx%d", pw_, ph_);
        p.set("preview-size", sz);
        p.set("picture-size", "640x480");
        return p;
    }
    void release() {
        previewing_ = false;
        if (fd_ >= 0) { say("close\n"); shutdown(fd_, 2); }
        if (thread_) { pthread_join(thread_, 0); thread_ = 0; }
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
    }
    int dump(int, const void *) const { return 0; }
    /* SEMC's additions: no zoom, no scene recognition, no face detection, focus as a plain auto focus */
    int semcCameraExtensionService() { return 0; }
    int setParametersInt(CameraParameters &, const char *, int) { return 0; }
    int setParametersStr(CameraParameters &, const char *, const char *) { return 0; }
    int setPreviewSize(CameraParameters &, int, int) { return 0; }
    int setPictureSize(CameraParameters &, int, int) { return 0; }
    int getExifInfo() { return 0; }
    /* SEMC's own focus request (CameraService::Client::startAutoFocus, libcameraservice 0xb894): its callback
     * (startAutoFocusCallback 0xca68) goes to JNICameraContext::startautofocus_callback_impl (libandroid_runtime
     * 0x52d5c), which fills SemcExtensionCamera$AutoFocusInfo {af_state, af_failure_state, multi_focus_state}.
     * framework.odex: CAMERA_AF_SUCCESS 0, AF_FAILURE_NONE 0; no multi-point focus here (0). */
    int startAutoFocus(void (*cb)(int, int, int, void *), void *user, bool, bool, bool) {
        LOG("startAutoFocus");
        saf_cb_ = cb; safUser_ = user;
        long t;
        incStrong(this);
        if (pthread_create(&t, 0, startFocused, this) != 0) { decStrong(this); return INVALID_OPERATION; }
        pthread_detach(t);
        return 0;
    }
    static void *startFocused(void *self) {
        AemuCamera *c = (AemuCamera *)self;
        if (c->saf_cb_) c->saf_cb_(0 /* CAMERA_AF_SUCCESS */, 0 /* AF_FAILURE_NONE */, 0, c->safUser_);
        c->decStrong(c);
        return 0;
    }
    int stopAutoFocus() { return 0; }
    int startAutoZoom(int, int, void (*)(int, int, void *), void *) { return INVALID_OPERATION; }
    int stopAutoZoom() { return 0; }
    int setZoom(int) { return 0; }
    int getMaxZoom(int, int) { return 0; }
    int setAeAfPosition(int, int, int, int, int) { return 0; }
    int getFocusPosition() { return 0; }
    int getStatus() { return 0; }
    int getPreviewSize(int *w, int *h) { if (w) *w = pw_; if (h) *h = ph_; return 0; }
    int getFaceDetectMode() { return 0; }
    int getSceneNvValue(void *, int) { return 0; }
    int setSceneRecognitionCallback(void *, void *) { return 0; }
    int setAbstractCallback(void (*)(int, void *), void *) { return 0; }
    int setSceneRecognitionInfo(void *, int) { return 0; }
    int setCollective() { return 0; }
    int convertImage864to854(unsigned char *, unsigned char *) { return 0; }
    int convertImage864to848(unsigned char *, unsigned char *) { return 0; }

private:
    void say(const char *s) {
        if (fd_ < 0) return;
        unsigned long n = strlen(s);
        while (n) { long r = write(fd_, s, n); if (r <= 0) return; s += r; n -= r; }
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
    /* NV21 (Y plane, then V,U pairs per 2x2) to YUYV (Y0 U Y1 V per pixel pair) */
    static void toYuyv(const uint8_t *src, uint8_t *dst, int w, int h) {
        const uint8_t *vu = src + w * h;
        for (int y = 0; y < h; y++) {
            const uint8_t *yr = src + y * w, *c = vu + (y >> 1) * w;
            uint8_t *d = dst + y * w * 2;
            for (int x = 0; x < w; x += 2, c += 2, d += 4) { d[0] = yr[x]; d[1] = c[1]; d[2] = yr[x + 1]; d[3] = c[0]; }
        }
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
        if (c->af_cb_) c->af_cb_(true, c->afUser_);
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
                int out = h[1] * h[2] * 2;
                if (out != bufSize_) {
                    for (int k = 0; k < NHEAP; k++) { buf_[k] = newMem(heap_[k], 0, out); busy_[k] = 0; }
                    bufSize_ = out;
                }
                for (int k = 0; k < NHEAP; k++) { int j = (next_ + k) % NHEAP; if (!busy_[j]) { i = j; break; } }
            }
            if (i >= 0) {
                next_ = (i + 1) % NHEAP;
                if (!nv21_ || !readAll(nv21_, len)) break;
                fw_ = h[1]; fh_ = h[2];
                toYuyv(nv21_, frame_[i], fw_, fh_);
                if (previewing_ && preview_cb_) preview_cb_(buf_[i], i, user_);
                if (recording_ && rec_cb_) {
                    struct { long s, ns; } t;
                    clock_gettime(1 /* CLOCK_MONOTONIC: systemTime() */, &t);
                    busy_[i] = 1;
                    rec_cb_((int64_t)t.s * 1000000000LL + t.ns, buf_[i], recUser_);
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
        shutter_callback s = shutter_cb_; picture_callback r = raw_cb_, j = jpeg_cb_; void *u = picUser_;
        shutter_cb_ = 0; raw_cb_ = 0; jpeg_cb_ = 0;
        if (s) s(u);
        if (r) {
            int pw = 4, ph = 3;
            pthread_mutex_lock(lock_);
            sizeOf(params_, "picture-size=", &pw, &ph);
            pthread_mutex_unlock(lock_);
            int tw = pw * 9 >= ph * 16 - ph / 8 ? RAWW_MAX : RAWH * 4 / 3;   /* 16:9 or 4:3 */
            postview((uint32_t *)raw_, tw, RAWH);
            r(newMem(rawHeap_, 0, tw * RAWH * 4), u);
        }
        if (j) {
            MemoryHeapBase *b;
            sp<IMemoryHeap> heap = newHeap(len, "aemu-jpeg", &b);
            memcpy(b->getBase(), jpg, len);
            sp<IMemory> mem = newMem(heap, 0, len);
            j(mem, u);
        }
        LOG("picture: %u bytes of JPEG", len);
    }

    /* the last preview frame (NV21, fw_ x fh_) cropped to w:h in the centre, scaled, as ARGB ints */
    void postview(uint32_t *out, int w, int h) {
        int sw = fw_, sh = fh_;
        if (!nv21_ || sw <= 0 || sh <= 0) { memset(out, 0, (unsigned long)w * h * 4); return; }
        int cw = sw, ch = sh;
        if ((long)sw * h > (long)sh * w) cw = (int)((long)sh * w / h); else ch = (int)((long)sw * h / w);
        int x0 = (sw - cw) / 2, y0 = (sh - ch) / 2;
        const uint8_t *yp = nv21_, *vu = nv21_ + sw * sh;
        for (int y = 0; y < h; y++) {
            int sy = y0 + y * ch / h;
            for (int x = 0; x < w; x++) {
                int sx = x0 + x * cw / w;
                int Y = yp[sy * sw + sx] - 16, V = vu[(sy / 2) * sw + (sx & ~1)] - 128, U = vu[(sy / 2) * sw + (sx & ~1) + 1] - 128;
                if (Y < 0) Y = 0;
                int R = (1192 * Y + 1634 * V) >> 10, G = (1192 * Y - 833 * V - 400 * U) >> 10, B = (1192 * Y + 2066 * U) >> 10;
                R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
                out[y * w + x] = 0xff000000u | (uint32_t)R << 16 | (uint32_t)G << 8 | (uint32_t)B;
            }
        }
    }

    int fd_;
    volatile bool previewing_, recording_;
    int pw_, ph_, bufSize_, sent_, overlay_, fw_, fh_;
    uint8_t *nv21_;
    volatile int busy_[NHEAP];
    long thread_;
    int next_;
    preview_callback preview_cb_;
    recording_callback rec_cb_;
    autofocus_callback af_cb_;
    void (*saf_cb_)(int, int, int, void *) = 0;
    void *safUser_ = 0;
    shutter_callback shutter_cb_;
    picture_callback raw_cb_, jpeg_cb_;
    void *user_, *recUser_, *afUser_, *picUser_;
    char *params_;
    long lock_[4];   /* pthread_mutex_t of bionic: one int, zero = unlocked */
    sp<IMemoryHeap> heap_[NHEAP], rawHeap_;
    sp<IMemory> buf_[NHEAP];
    uint8_t *frame_[NHEAP], *raw_;
};

}  // namespace

/* sp<CameraHardwareInterface> openCameraHardware(): the caller passes the place for the result */
extern "C" EXPORT void openCameraHardware(sp<CameraHardwareInterface> *result) {
    new (result) sp<CameraHardwareInterface>(new AemuCamera());
}
