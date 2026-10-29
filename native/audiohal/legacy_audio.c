/*
 * libaudio.so for Android 2.x mediaservers (the C++ AudioHardwareInterface of hardware_legacy, before audio.primary):
 * the output to /dev/eac like the firmware's own AudioHardwareGeneric, and a microphone at any rate the application
 * asks for, from the phone's microphone (/dev/aemu_mic_ctl + /dev/aemu_mic, AudioIn.kt). AudioHardwareGeneric only
 * takes 8000 Hz mono input (AudioHardwareBase::getInputBufferSize, AudioStreamInGeneric::set), so recorders that
 * want 44.1 kHz (camcorders) fail to start with it.
 *
 * The virtual tables follow the firmware: vendors add methods to AudioStreamOut (HTC: four after setVolume, three after
 * getRenderPosition), so the order of every table is read when the HAL is installed (TreeFixer, LegacyAudio.kt) from
 * the firmware's own AudioHardwareGeneric / AudioStreamOutGeneric / AudioStreamInGeneric tables and given here in
 * /system/etc/aemu_legacy_audio.map, one line per class: "<class> <n> <method>…", "-" for a method we do not know
 * (the vendors' additions are "return 0" in their base classes; these get the same).
 *
 * Calls are the ARM EABI C++ ones: `this` in r0, a String8 returned by value through a pointer in r0 (`this` in r1),
 * float arguments in core registers (softfp).
 */
#include <stddef.h>
#include <stdint.h>

extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int close(int);
extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void free(void *);
extern int usleep(unsigned long);
extern volatile int *__errno(void);
extern int __android_log_print(int, const char *, const char *, ...);
/* android::String8::String8(const char *) of the firmware's libutils */
extern void _ZN7android7String8C1EPKc(void *self, const char *s);

#define EXPORT __attribute__((visibility("default")))
#define LOG(...) __android_log_print(4, "aemu-audio", __VA_ARGS__)
#define O_RDONLY 0
#define O_RDWR 2
#define O_NONBLOCK 04000
#define EINTR 4
#define BAD_VALUE (-22)
#define INVALID_OPERATION (-38)

/* AudioSystem of 2.x (the firmware's AudioStreamOutGeneric / AudioStreamInGeneric return these) */
#define PCM_16_BIT 1
#define CH_OUT_STEREO 0x0c
#define CH_IN_MONO 0x10
#define CH_IN_STEREO 0x0c
#define OUT_RATE 44100   /* what AudioOut reads from /dev/eac for 2.x */
#define OUT_BUF 4096

typedef void (*fnp)(void);
enum { HW, OUT, IN, NCLS };
#define MAXSLOTS 40
static fnp g_vt[NCLS][MAXSLOTS];
static int g_ok;

struct obj { fnp *vt; };
struct hw { fnp *vt; int fd; int mic_mute; void *out, *in; int mode; uint32_t routes[3]; };
struct out { fnp *vt; struct hw *hw; };
struct in { fnp *vt; struct hw *hw; int fd, started, channels; uint32_t rate, mask; };

static unsigned long udiv(unsigned long n, unsigned long d) {
    unsigned long q = 0, r = 0;
    for (int b = 31; b >= 0; b--) { r = (r << 1) | ((n >> b) & 1); if (r >= d) { r -= d; q |= 1UL << b; } }
    return q;
}
static int popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
static int rate_ok(uint32_t r) { return r == 8000 || r == 11025 || r == 16000 || r == 22050 || r == 32000 || r == 44100 || r == 48000; }
static size_t in_bytes(uint32_t rate, int ch) { size_t b = (size_t)udiv(rate, 50) * (size_t)(ch == 2 ? 4 : 2); return (b + 63) & ~(size_t)63; }  /* 20 ms */
static void str8(void *ret, const char *s) { _ZN7android7String8C1EPKc(ret, s); }

static long ret0(void) { return 0; }

/* ---- the hardware ---- */
static void hw_dtor(struct hw *h) { if (h->fd >= 0) close(h->fd); }
static void hw_dtor0(struct hw *h) { hw_dtor(h); free(h); }
static int hw_init(struct hw *h) { return h->fd >= 0 ? 0 : -19; }
static int hw_voice(struct hw *h, float v) { (void)h; (void)v; return 0; }
static int hw_master(struct hw *h, float v) { (void)h; (void)v; return INVALID_OPERATION; }   /* AudioFlinger then scales in software */
static int hw_mode(struct hw *h, int m) { h->mode = m; return 0; }
/* 1.6 (Donut AudioHardwareInterface: setRouting/getRouting/getMode/setParameter/doRouting); routes per mode as the
   firmware's AudioHardwareBase keeps them (Sony X10 libaudio.so 0x5788 setRouting / 0x57bc getRouting: MODE_CURRENT -1,
   NUM_MODES 3, else BAD_VALUE) */
static int hw_setrouting(struct hw *h, int mode, uint32_t routes) {
    if (mode == -1) mode = h->mode;
    if (mode < 0 || mode > 2) return BAD_VALUE;
    h->routes[mode] = routes;
    return 0;
}
static int hw_getrouting(struct hw *h, int mode, uint32_t *routes) {
    if (mode == -1) mode = h->mode;
    if (mode < 0 || mode > 2) return BAD_VALUE;
    if (routes) *routes = h->routes[mode];
    return 0;
}
static int hw_getmode(struct hw *h, int *m) { if (m) *m = h->mode; return 0; }
static int hw_setparam(struct hw *h, const char *k, const char *v) { (void)h; (void)k; (void)v; return 0; }
static int hw_setmute(struct hw *h, int m) { h->mic_mute = m & 0xff; return 0; }
static int hw_getmute(struct hw *h, uint8_t *m) { if (m) *m = (uint8_t)h->mic_mute; return 0; }
static int hw_setparams(struct hw *h, const void *kv) { (void)h; (void)kv; return 0; }
static void hw_getparams(void *ret, struct hw *h, const void *keys) { (void)h; (void)keys; str8(ret, ""); }
static size_t hw_inbuf(struct hw *h, uint32_t rate, int format, int ch) {
    (void)h;
    if (format != PCM_16_BIT || (ch != 1 && ch != 2) || !rate_ok(rate)) return 0;
    return in_bytes(rate, ch);
}
static int hw_dump(struct hw *h, int fd, const void *args) { (void)h; (void)fd; (void)args; return 0; }

/* ---- output: PCM 16-bit stereo 44.1 kHz into /dev/eac ---- */
static void out_dtor(struct out *o) { if (o->hw && o->hw->out == o) o->hw->out = 0; }
static void out_dtor0(struct out *o) { out_dtor(o); free(o); }
static uint32_t out_rate(struct out *o) { (void)o; return OUT_RATE; }
static size_t out_bufsize(struct out *o) { (void)o; return OUT_BUF; }
static uint32_t out_channels(struct out *o) { (void)o; return CH_OUT_STEREO; }
static int out_format(struct out *o) { (void)o; return PCM_16_BIT; }
static uint32_t out_latency(struct out *o) { (void)o; return 20; }
static int out_volume(struct out *o, float l, float r) { (void)o; (void)l; (void)r; return INVALID_OPERATION; }
static long out_write(struct out *o, const void *buf, size_t bytes) {
    int fd = o->hw->fd;
    if (fd < 0) { usleep(udiv((unsigned long)bytes * 1000UL, OUT_RATE * 4 / 1000)); return (long)bytes; }
    const char *p = (const char *)buf;
    size_t left = bytes;
    while (left) {
        long n = write(fd, p, left);
        if (n < 0 && *__errno() == EINTR) continue;
        if (n <= 0) break;
        p += n; left -= (size_t)n;
    }
    return (long)bytes;
}
static int out_standby(struct out *o) { (void)o; return 0; }
static int out_dump(struct out *o, int fd, const void *a) { (void)o; (void)fd; (void)a; return 0; }
static int out_setparams(struct out *o, const void *kv) { (void)o; (void)kv; return 0; }
static void out_getparams(void *ret, struct out *o, const void *keys) { (void)o; (void)keys; str8(ret, ""); }
static int out_renderpos(struct out *o, uint32_t *f) { (void)o; (void)f; return INVALID_OPERATION; }

/* ---- input: the phone's microphone at the asked rate (AudioIn.kt) ---- */
static void mic_ctl(const char *what, uint32_t rate, int ch) {
    char line[48];
    int k = 0;
    for (const char *c = what; *c; c++) line[k++] = *c;
    if (rate) {
        char t[12]; int n = 0; uint32_t v = rate;
        line[k++] = ' ';
        do { uint32_t q = (uint32_t)udiv(v, 10); t[n++] = (char)('0' + (v - q * 10)); v = q; } while (v);
        while (n) line[k++] = t[--n];
        line[k++] = ' '; line[k++] = (char)('0' + ch);
    }
    line[k++] = 10;
    int fd = open("/dev/aemu_mic_ctl", O_RDWR | O_NONBLOCK);
    if (fd >= 0) { write(fd, line, (unsigned long)k); close(fd); }
}
static int in_standby(struct in *i) {
    if (i->started) { mic_ctl("stop", 0, 0); i->started = 0; }
    return 0;
}
static void in_dtor(struct in *i) {
    in_standby(i);
    if (i->fd >= 0) close(i->fd);
    if (i->hw && i->hw->in == i) i->hw->in = 0;
}
static void in_dtor0(struct in *i) { in_dtor(i); free(i); }
static uint32_t in_rate(struct in *i) { return i->rate; }
static size_t in_bufsize(struct in *i) { return in_bytes(i->rate, i->channels); }
static uint32_t in_channels(struct in *i) { return i->mask; }
static int in_format(struct in *i) { (void)i; return PCM_16_BIT; }
static int in_gain(struct in *i, float g) { (void)i; (void)g; return 0; }
static long in_read(struct in *i, void *buf, long bytes) {
    if (bytes <= 0) return 0;
    if (!i->started) { mic_ctl("start", i->rate, i->channels); i->started = 1; }
    if (i->fd < 0) i->fd = open("/dev/aemu_mic", O_RDWR | O_NONBLOCK);
    unsigned long frames = (unsigned long)bytes / (i->channels == 2 ? 4UL : 2UL);
    unsigned long dur_us = udiv(frames * 1000UL, i->rate) * 1000UL;
    char *p = (char *)buf;
    long got = 0;
    unsigned long waited = 0;
    while (got < bytes && i->fd >= 0) {
        long n = read(i->fd, p + got, (unsigned long)(bytes - got));
        if (n > 0) { got += n; continue; }
        if (n < 0 && *__errno() == EINTR) continue;
        if (waited >= dur_us + dur_us / 2) break;   /* host late or absent: the rest is silence */
        usleep(2000); waited += 2000;
    }
    if (got < bytes || (i->hw && i->hw->mic_mute)) for (long k = (i->hw && i->hw->mic_mute) ? 0 : got; k < bytes; k++) p[k] = 0;
    if (i->fd < 0) usleep(dur_us);
    return bytes;
}
static int in_dump(struct in *i, int fd, const void *a) { (void)i; (void)fd; (void)a; return 0; }
static int in_setparams(struct in *i, const void *kv) { (void)i; (void)kv; return 0; }
static void in_getparams(void *ret, struct in *i, const void *keys) { (void)i; (void)keys; str8(ret, ""); }
static uint32_t in_lost(struct in *i) { (void)i; return 0; }

/* ---- streams ---- */
static void *hw_open_out(struct hw *h, uint32_t devices, int *format, uint32_t *channels, uint32_t *rate, int *status) {
    (void)devices;
    int bad = (format && *format && *format != PCM_16_BIT) || (channels && *channels && *channels != CH_OUT_STEREO) ||
              (rate && *rate && *rate != OUT_RATE);
    if (format) *format = PCM_16_BIT;
    if (channels) *channels = CH_OUT_STEREO;
    if (rate) *rate = OUT_RATE;
    if (bad || h->out) { if (status) *status = bad ? BAD_VALUE : INVALID_OPERATION; return 0; }
    struct out *o = calloc(1, sizeof *o);
    if (!o) { if (status) *status = -12; return 0; }
    o->vt = g_vt[OUT]; o->hw = h; h->out = o;
    if (status) *status = 0;
    return o;
}
static void hw_close_out(struct hw *h, struct obj *o) { (void)h; if (o) ((void (*)(void *))o->vt[1])(o); }

static void *hw_open_in(struct hw *h, uint32_t devices, int *format, uint32_t *channels, uint32_t *rate, int *status, int acoustics) {
    (void)devices; (void)acoustics;
    uint32_t r = rate && *rate ? *rate : 8000;
    uint32_t m = channels && *channels ? *channels : CH_IN_MONO;
    int ch = popcount(m);
    int bad = (format && *format && *format != PCM_16_BIT) || !rate_ok(r) || ch < 1 || ch > 2;
    if (bad) {
        if (format) *format = PCM_16_BIT;
        if (rate) *rate = rate_ok(r) ? r : 44100;
        if (channels) *channels = ch == 2 ? CH_IN_STEREO : CH_IN_MONO;
        if (status) *status = BAD_VALUE;   /* AudioFlinger::openInput retries with these */
        return 0;
    }
    if (h->in) { if (status) *status = INVALID_OPERATION; return 0; }
    struct in *i = calloc(1, sizeof *i);
    if (!i) { if (status) *status = -12; return 0; }
    i->vt = g_vt[IN]; i->hw = h; i->fd = -1; i->rate = r; i->mask = m; i->channels = ch;
    h->in = i;
    if (format) *format = PCM_16_BIT;
    if (rate) *rate = r;
    if (channels) *channels = m;
    if (status) *status = 0;
    LOG("microphone %u Hz, %d channel(s)", r, ch);
    return i;
}
static void hw_close_in(struct hw *h, struct obj *i) { (void)h; if (i) ((void (*)(void *))i->vt[1])(i); }

/* 1.6: openOutputStream(int format, int channelCount, uint32_t sampleRate, status_t *status) and
   openInputStream(int inputSource, int format, int channelCount, uint32_t sampleRate, status_t *status, acoustics)
   take values, not pointers (Sony X10 libaudio.so: AudioHardwareGeneric::openOutputStreamEiijPi, openInputStreamEiiijPi…) */
static void *hw_open_out16(struct hw *h, int format, int count, uint32_t rate, int *status) {
    int st = 0;
    void *o = 0;
    if ((format && format != PCM_16_BIT) || (count && count != 2) || (rate && rate != OUT_RATE)) st = BAD_VALUE;
    else { uint32_t ch = 0; int f = 0; uint32_t r = 0; o = hw_open_out(h, 0, &f, &ch, &r, &st); }
    if (status) *status = st;
    return o;
}
static void *hw_open_in16(struct hw *h, int source, int format, int count, uint32_t rate, int *status, int acoustics) {
    (void)source;
    int f = format, st = 0;
    uint32_t r = rate, ch = count == 2 ? CH_IN_STEREO : CH_IN_MONO;
    void *i = 0;
    if (count < 0 || count > 2) st = BAD_VALUE; else i = hw_open_in(h, 0, &f, &ch, &r, &st, acoustics);
    if (status) *status = st;
    return i;
}
static int out_chcount(struct out *o) { (void)o; return 2; }
static int in_chcount(struct in *i) { return i->channels; }

/* ---- the tables, by the firmware's order ---- */
struct meth { const char *name; fnp fn; };
static const struct meth M_HW[] = {
    { "D1", (fnp)hw_dtor }, { "D2", (fnp)hw_dtor }, { "D0", (fnp)hw_dtor0 }, { "initCheck", (fnp)hw_init },
    { "setVoiceVolume", (fnp)hw_voice }, { "setMasterVolume", (fnp)hw_master }, { "setMode", (fnp)hw_mode },
    { "setMicMute", (fnp)hw_setmute }, { "getMicMute", (fnp)hw_getmute }, { "setParameters", (fnp)hw_setparams },
    { "getParameters", (fnp)hw_getparams }, { "getInputBufferSize", (fnp)hw_inbuf },
    { "openOutputStream", (fnp)hw_open_out }, { "closeOutputStream", (fnp)hw_close_out },
    { "openInputStream", (fnp)hw_open_in }, { "closeInputStream", (fnp)hw_close_in },
    { "dumpState", (fnp)hw_dump }, { "dump", (fnp)hw_dump },
    { "setRouting", (fnp)hw_setrouting }, { "getRouting", (fnp)hw_getrouting }, { "getMode", (fnp)hw_getmode },
    { "setParameter", (fnp)hw_setparam }, { "doRouting", (fnp)ret0 },
    { "openOutputStream16", (fnp)hw_open_out16 }, { "openInputStream16", (fnp)hw_open_in16 }, { 0, 0 } };
static const struct meth M_OUT[] = {
    { "D1", (fnp)out_dtor }, { "D2", (fnp)out_dtor }, { "D0", (fnp)out_dtor0 }, { "sampleRate", (fnp)out_rate },
    { "bufferSize", (fnp)out_bufsize }, { "channels", (fnp)out_channels }, { "format", (fnp)out_format },
    { "latency", (fnp)out_latency }, { "setVolume", (fnp)out_volume }, { "write", (fnp)out_write },
    { "standby", (fnp)out_standby }, { "dump", (fnp)out_dump }, { "setParameters", (fnp)out_setparams },
    { "getParameters", (fnp)out_getparams }, { "getRenderPosition", (fnp)out_renderpos },
    { "channelCount", (fnp)out_chcount }, { 0, 0 } };
static const struct meth M_IN[] = {
    { "D1", (fnp)in_dtor }, { "D2", (fnp)in_dtor }, { "D0", (fnp)in_dtor0 }, { "sampleRate", (fnp)in_rate },
    { "bufferSize", (fnp)in_bufsize }, { "channels", (fnp)in_channels }, { "format", (fnp)in_format },
    { "setGain", (fnp)in_gain }, { "read", (fnp)in_read }, { "dump", (fnp)in_dump }, { "standby", (fnp)in_standby },
    { "setParameters", (fnp)in_setparams }, { "getParameters", (fnp)in_getparams },
    { "getInputFramesLost", (fnp)in_lost }, { "channelCount", (fnp)in_chcount }, { 0, 0 } };

static int streq_n(const char *a, const char *b, int n) {
    for (int k = 0; k < n; k++) if (a[k] != b[k]) return 0;
    return b[n] == 0;
}
static fnp lookup(const struct meth *m, const char *w, int n) {
    for (; m->name; m++) if (streq_n(w, m->name, n)) return m->fn;
    return (fnp)ret0;
}

/* "hw 19 D1 D0 initCheck …" lines */
static int load_map(void) {
    int fd = open("/system/etc/aemu_legacy_audio.map", O_RDONLY);
    if (fd < 0) return 0;
    static char buf[4096];
    long n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    int seen = 0;
    for (char *p = buf; *p; ) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        int cls = line[0] == 'h' ? HW : line[0] == 'o' ? OUT : line[0] == 'i' ? IN : -1;
        if (cls < 0) continue;
        const struct meth *m = cls == HW ? M_HW : cls == OUT ? M_OUT : M_IN;
        char *w = line;
        while (*w && *w != ' ') w++;   /* class */
        while (*w == ' ') w++;
        while (*w && *w != ' ') w++;   /* count */
        int slot = 0;
        while (*w) {
            while (*w == ' ') w++;
            if (!*w) break;
            char *s = w;
            while (*w && *w != ' ') w++;
            if (slot < MAXSLOTS) g_vt[cls][slot++] = lookup(m, s, (int)(w - s));
        }
        for (; slot < MAXSLOTS; slot++) g_vt[cls][slot] = (fnp)ret0;
        seen |= 1 << cls;
    }
    return seen == 7;
}

static void *make_hw(void) {
    if (!g_ok) g_ok = load_map() ? 1 : -1;
    if (g_ok < 0) { LOG("no /system/etc/aemu_legacy_audio.map"); return 0; }
    struct hw *h = calloc(1, sizeof *h);
    if (!h) return 0;
    h->vt = g_vt[HW];
    h->fd = open("/dev/eac", O_RDWR);
    LOG("the emulator's audio HAL: /dev/eac %s, microphone from the host", h->fd >= 0 ? "open" : "missing");
    return h;
}

/* AudioHardwareInterface* AudioHardwareInterface::create() — vendors whose libaudio carries it (HTC) */
EXPORT void *_ZN7android22AudioHardwareInterface6createEv(void) { return make_hw(); }
/* AudioHardwareInterface* createAudioHardware() — called by libaudioflinger's own create() */
EXPORT void *_ZN7android19createAudioHardwareEv(void) { return make_hw(); }
EXPORT void *createAudioHardware(void) { return make_hw(); }

/*
 * Android 1.6: libaudioflinger derives its own interfaces (A2dpAudioInterface, AudioDumpInterface) from
 * AudioHardwareBase and AudioStreamOut, which live in libaudio.so (Donut hardware_legacy/AudioHardwareInterface.h);
 * it imports the constructor, the vtable and the non-pure methods. Layout and behaviour follow the firmware's own
 * (Sony X10 libaudio.so, prelink 0x6b700000): object = vptr, mMode @+4, mRoutes[3] @+8 (C2Ev 0x59d4 zeroes 12 bytes
 * at +8 and mMode); the vtable at 0x8170, 18 slots after offset-to-top/typeinfo, doRouting at +0x40, dump at +0x44.
 */
struct base { fnp *vt; int mode; uint32_t routes[3]; };
static int base_pure(void) { LOG("pure virtual called"); return INVALID_OPERATION; }
static int base_doRouting(struct base *b) { return ((int (*)(struct base *))b->vt[16])(b); }
/* 0x5788: an unchanged route or another mode than the current one → NO_ERROR, else doRouting */
EXPORT int _ZN7android17AudioHardwareBase10setRoutingEij(struct base *b, int mode, uint32_t routes) {
    if (mode == -1) mode = b->mode;
    if (mode < 0 || mode > 2) return BAD_VALUE;
    uint32_t old = b->routes[mode];
    b->routes[mode] = routes;
    if (mode != b->mode || old == routes) return 0;
    return base_doRouting(b);
}
EXPORT int _ZN7android17AudioHardwareBase10getRoutingEiPj(struct base *b, int mode, uint32_t *routes) {
    if (mode == -1) mode = b->mode;
    if (mode < 0 || mode > 2) return BAD_VALUE;
    *routes = b->routes[mode];
    return 0;
}
/* 0x57dc: the same mode → NO_ERROR, else store it and doRouting */
EXPORT int _ZN7android17AudioHardwareBase7setModeEi(struct base *b, int mode) {
    if (mode < 0 || mode > 2) return BAD_VALUE;
    if (b->mode == mode) return 0;
    b->mode = mode;
    return base_doRouting(b);
}
EXPORT int _ZN7android17AudioHardwareBase7getModeEPi(struct base *b, int *mode) { *mode = b->mode; return 0; }
EXPORT int _ZN7android17AudioHardwareBase12setParameterEPKcS2_(struct base *b, const char *k, const char *v) { (void)b; (void)k; (void)v; return 0; }
/* 0x5960: only 8000 Hz mono; format 3 → 1280, 4 → 1150, 5 → 1050 bytes, others (PCM) 2048 */
EXPORT size_t _ZN7android17AudioHardwareBase18getInputBufferSizeEjii(struct base *b, uint32_t rate, int format, int count) {
    (void)b;
    if (rate != 8000 || count != 1) return 0;
    return format == 3 ? 1280 : format == 5 ? 1050 : format == 4 ? 1150 : 2048;
}
EXPORT int _ZN7android17AudioHardwareBase9dumpStateEiRKNS_6VectorINS_8String16EEE(struct base *b, int fd, const void *args) {
    return ((int (*)(struct base *, int, const void *))b->vt[17])(b, fd, args);
}
static void *base_dtor(struct base *b);
static void *base_dtor0(struct base *b) { base_dtor(b); free(b); return b; }
EXPORT fnp _ZTVN7android17AudioHardwareBaseE[20] = {
    0, 0, (fnp)base_dtor, (fnp)base_dtor0, (fnp)base_pure, (fnp)base_pure, (fnp)base_pure,
    (fnp)_ZN7android17AudioHardwareBase10setRoutingEij, (fnp)_ZN7android17AudioHardwareBase10getRoutingEiPj,
    (fnp)_ZN7android17AudioHardwareBase7setModeEi, (fnp)_ZN7android17AudioHardwareBase7getModeEPi,
    (fnp)base_pure, (fnp)base_pure, (fnp)_ZN7android17AudioHardwareBase12setParameterEPKcS2_,
    (fnp)_ZN7android17AudioHardwareBase18getInputBufferSizeEjii, (fnp)base_pure, (fnp)base_pure,
    (fnp)_ZN7android17AudioHardwareBase9dumpStateEiRKNS_6VectorINS_8String16EEE, (fnp)base_pure, (fnp)base_pure };
static void *base_dtor(struct base *b) { b->vt = _ZTVN7android17AudioHardwareBaseE + 2; return b; }
EXPORT struct base *_ZN7android17AudioHardwareBaseC1Ev(struct base *b) {
    b->vt = _ZTVN7android17AudioHardwareBaseE + 2;
    b->routes[0] = b->routes[1] = b->routes[2] = 0;
    b->mode = 0;
    return b;
}
EXPORT struct base *_ZN7android17AudioHardwareBaseC2Ev(struct base *b) { return _ZN7android17AudioHardwareBaseC1Ev(b); }
/* AudioStreamOut::~AudioStreamOut (0x586c: resets the vptr to the abstract class's table, 12 slots) */
static fnp g_out_abstract[14] = { 0, 0, (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0,
    (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0, (fnp)ret0 };
EXPORT void *_ZN7android14AudioStreamOutD1Ev(struct obj *o) { o->vt = g_out_abstract + 2; return o; }
EXPORT void *_ZN7android14AudioStreamOutD2Ev(struct obj *o) { return _ZN7android14AudioStreamOutD1Ev(o); }
