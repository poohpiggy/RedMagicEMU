/*
 * libOmxVidEnc.so for Qualcomm 1.x-2.x firmwares (QSD8x50/MSM7x30 OMX core, libOmxCore.so): the video encoder
 * components OMX.qcom.video.encoder.mpeg4 / .h263, encoding on the phone (VideoEncoderFeed.kt over
 * /dev/socket/aemu_venc). The firmware's own library drives the Q6 DSP (/dev/q6venc: it gets run-length data back and
 * packs the stream itself, qvp_encode_*_after_rlc), which the emulator does not have.
 *
 * Loading contract (libOmxCore.so of the Sony X10 1.6, disassembled): the core dlopens the library and calls
 * get_omx_component_factory_fn(), which returns a qc_omx_component (AOSP mm-core qc_omx_component.h): vptr at +0,
 * OMX_COMPONENTTYPE m_cmp at +4 (0x58 bytes, filled by qc_omx_create_component_wrapper with its own entry points,
 * pComponentPrivate = the object); every entry point calls the virtual method with (this, hComp, args…).
 * Slots (qc_omx_component_* wrappers): 0 ~dtor, 1 deleting dtor, 2 component_init, 3 get_component_version,
 * 4 send_command, 5 get_parameter, 6 set_parameter, 7 get_config, 8 set_config, 9 get_extension_index, 10 get_state,
 * 11 component_tunnel_request, 12 use_buffer, 13 allocate_buffer, 14 free_buffer, 15 empty_this_buffer,
 * 16 fill_this_buffer, 17 set_callbacks, 18 component_deinit, 19 use_EGL_image, 20 component_role_enum.
 * Behaviour: OMX IL 1.1 (OMX_Core.h, OMX_Component.h, OMX_Video.h, OMX_Index.h of that release).
 */
#include <stddef.h>
#include <stdint.h>

extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void free(void *);
extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);
extern int strcmp(const char *, const char *);
extern unsigned long strlen(const char *);
extern char *strncpy(char *, const char *, unsigned long);
extern int socket(int, int, int);
extern int connect(int, const void *, unsigned);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int close(int);
extern volatile int *__errno(void);
extern int __android_log_print(int, const char *, const char *, ...);
typedef long pthread_t;
typedef struct { int v; } pthread_mutex_t;
typedef struct { int v; } pthread_cond_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
extern int pthread_mutex_init(pthread_mutex_t *, const void *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_cond_init(pthread_cond_t *, const void *);
extern int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int pthread_cond_signal(pthread_cond_t *);

#define EXPORT __attribute__((visibility("default")))
#define LOG(...) __android_log_print(4, "aemu-venc", __VA_ARGS__)
#define EINTR 4

typedef uint32_t U32;
typedef int OMX_ERR;
enum {
    OMX_ErrorNone = 0, OMX_ErrorInsufficientResources = (int)0x80001000, OMX_ErrorUndefined = (int)0x80001001,
    OMX_ErrorBadParameter = (int)0x80001005, OMX_ErrorNotImplemented = (int)0x80001006,
    OMX_ErrorInvalidState = (int)0x8000100A, OMX_ErrorUnsupportedIndex = (int)0x8000101A,
    OMX_ErrorBadPortIndex = (int)0x8000101B, OMX_ErrorIncorrectStateTransition = (int)0x80001017,
    OMX_ErrorIncorrectStateOperation = (int)0x80001018, OMX_ErrorSameState = (int)0x80001010,
    OMX_ErrorNoMore = (int)0x8000100E, OMX_ErrorHardware = (int)0x80001009,
};
enum { ST_INVALID, ST_LOADED, ST_IDLE, ST_EXECUTING, ST_PAUSE, ST_WAIT };
enum { CMD_STATE, CMD_FLUSH, CMD_PORT_DISABLE, CMD_PORT_ENABLE, CMD_MARK };
enum { EV_CMD_COMPLETE, EV_ERROR, EV_MARK, EV_PORT_SETTINGS, EV_BUFFER_FLAG };
#define FLAG_EOS 0x1
#define FLAG_SYNCFRAME 0x20
#define FLAG_ENDOFFRAME 0x10
#define FLAG_CODECCONFIG 0x80
#define ALL_PORTS 0xFFFFFFFFu

/* OMX_Index.h (1.1) */
#define IDX_PARAM_PRIORITY 0x01000001
#define IDX_PARAM_AUDIO_INIT 0x01000002
#define IDX_PARAM_IMAGE_INIT 0x01000003
#define IDX_PARAM_VIDEO_INIT 0x01000004
#define IDX_PARAM_OTHER_INIT 0x01000005
#define IDX_PARAM_ROLE 0x01000017
#define IDX_PARAM_PORTDEF 0x02000001
#define IDX_PARAM_SUPPLIER 0x02000002
#define IDX_VIDEO_PORTFORMAT 0x06000001
#define IDX_VIDEO_QUANT 0x06000002
#define IDX_VIDEO_BITRATE 0x06000004
#define IDX_VIDEO_MOTION 0x06000005
#define IDX_VIDEO_INTRAREFRESH 0x06000006
#define IDX_VIDEO_ERRORCORR 0x06000007
#define IDX_VIDEO_MPEG4 0x0600000A
#define IDX_VIDEO_H263 0x0600000E
#define IDX_VIDEO_PROFILE_QUERY 0x0600000F
#define IDX_VIDEO_PROFILE_CUR 0x06000010
#define IDX_CONFIG_BITRATE 0x06000011
#define IDX_CONFIG_FRAMERATE 0x06000012
#define IDX_CONFIG_INTRAVOP 0x06000013

#define COLOR_YUV420SP 0x15   /* OMX_COLOR_FormatYUV420SemiPlanar */
#define CODING_H263 3
#define CODING_MPEG4 4

/* OMX_PARAM_PORTDEFINITIONTYPE (32-bit): 96 bytes */
struct portdef {
    U32 nSize, nVersion, nPortIndex, eDir, nBufferCountActual, nBufferCountMin, nBufferSize, bEnabled, bPopulated, eDomain;
    const char *cMIMEType; void *pNativeRender; U32 nFrameWidth, nFrameHeight; int nStride; U32 nSliceHeight, nBitrate,
        xFramerate, bFlagErrorConcealment, eCompressionFormat, eColorFormat; void *pNativeWindow;
    U32 bBuffersContiguous, nBufferAlignment;
};
/* OMX_BUFFERHEADERTYPE: nTimeStamp (OMX_TICKS, 64-bit) is 8-aligned under the ARM EABI */
struct bufhdr {
    U32 nSize, nVersion; uint8_t *pBuffer; U32 nAllocLen, nFilledLen, nOffset; void *pAppPrivate, *pPlatformPrivate,
        *pInputPortPrivate, *pOutputPortPrivate, *hMarkTargetComponent, *pMarkData; U32 nTickCount, pad;
    int64_t nTimeStamp; U32 nFlags, nOutputPortIndex, nInputPortIndex;
};
struct callbacks {
    OMX_ERR (*EventHandler)(void *h, void *app, U32 ev, U32 d1, U32 d2, void *data);
    OMX_ERR (*EmptyBufferDone)(void *h, void *app, struct bufhdr *b);
    OMX_ERR (*FillBufferDone)(void *h, void *app, struct bufhdr *b);
};

#define MAXBUF 32
#define MAXPARAM 24
struct port { struct portdef def; struct bufhdr *buf[MAXBUF]; uint8_t own[MAXBUF]; int nbuf; };
struct param { U32 index, port, size; void *data; };
enum { Q_CMD, Q_ETB, Q_FTB };
struct qitem { int kind; U32 a, b; struct bufhdr *buf; struct qitem *next; };

struct venc {
    void **vptr;
    U32 cmp[22];                       /* OMX_COMPONENTTYPE m_cmp, filled by libOmxCore */
    void *hcomp;
    struct callbacks cb; void *app;
    int state, pending;                /* pending: target of a state change waiting for buffers (or -1) */
    int coding;
    char role[32];
    struct port port[2];
    struct param params[MAXPARAM]; int nparams;
    pthread_mutex_t lock; pthread_cond_t cond;
    struct qitem *qhead, *qtail;
    struct bufhdr *in[MAXBUF]; int nin;     /* input waiting */
    struct bufhdr *out[MAXBUF]; int nout;   /* output waiting */
    pthread_t thread; int quit;
    int fd, configured; U32 bitrate, fps;
    uint8_t *pend; U32 pendLen, pendCap; int64_t pendTs; U32 pendFlags; int havePend;
};

static void *g_vt[21];

/* ---------------- the host channel: [u32 type][u32 len] + payload ---------------- */
static int put_all(int fd, const void *p, unsigned long n) {
    const char *c = (const char *)p;
    while (n) { long k = write(fd, c, n); if (k < 0 && *__errno() == EINTR) continue; if (k <= 0) return 0; c += k; n -= (unsigned long)k; }
    return 1;
}
static int get_all(int fd, void *p, unsigned long n) {
    char *c = (char *)p;
    while (n) { long k = read(fd, c, n); if (k < 0 && *__errno() == EINTR) continue; if (k <= 0) return 0; c += k; n -= (unsigned long)k; }
    return 1;
}
static int host_open(void) {
    struct { unsigned short fam; char path[108]; } sa;
    memset(&sa, 0, sizeof sa);
    sa.fam = 1;
    const char *p = "/dev/socket/aemu_venc";
    for (int i = 0; p[i]; i++) sa.path[i] = p[i];
    int fd = socket(1, 1 | 02000000, 0);
    if (fd < 0) return -1;
    if (connect(fd, &sa, (unsigned)(2 + strlen(p) + 1)) < 0) { close(fd); return -1; }
    return fd;
}

/* ---------------- parameters ---------------- */
static struct param *pfind(struct venc *v, U32 idx, U32 port) {
    for (int i = 0; i < v->nparams; i++) if (v->params[i].index == idx && v->params[i].port == port) return &v->params[i];
    return 0;
}
static void pstore(struct venc *v, U32 idx, U32 port, const void *data, U32 size) {
    struct param *p = pfind(v, idx, port);
    if (!p) { if (v->nparams >= MAXPARAM) return; p = &v->params[v->nparams++]; p->index = idx; p->port = port; p->data = 0; p->size = 0; }
    if (p->size < size) { free(p->data); p->data = malloc(size); }
    if (!p->data) { p->size = 0; return; }
    memcpy(p->data, data, size); p->size = size;
}
static void set_portdef(struct venc *v, int i, U32 w, U32 h) {
    struct portdef *d = &v->port[i].def;
    d->nFrameWidth = w; d->nFrameHeight = h; d->nStride = (int)w; d->nSliceHeight = h;
    if (i == 0) d->nBufferSize = w * h * 3 / 2;
    else d->nBufferSize = w * h * 3 / 2 > (256u << 10) ? w * h * 3 / 2 : (256u << 10);
}
static void init_ports(struct venc *v) {
    for (int i = 0; i < 2; i++) {
        struct portdef *d = &v->port[i].def;
        memset(d, 0, sizeof *d);
        d->nSize = sizeof *d; d->nVersion = 0x00000101; d->nPortIndex = (U32)i; d->eDir = (U32)i;   /* input 0, output 1 */
        d->nBufferCountActual = i ? 4 : 4; d->nBufferCountMin = 2; d->bEnabled = 1; d->eDomain = 2; /* video */
        d->cMIMEType = i ? (v->coding == CODING_H263 ? "video/3gpp" : "video/mpeg4") : "video/raw";
        d->xFramerate = 15u << 16; d->nBitrate = i ? 384000 : 0;
        d->eCompressionFormat = i ? (U32)v->coding : 0;   /* OMX_VIDEO_CodingUnused for raw */
        d->eColorFormat = i ? 0 : COLOR_YUV420SP;
        d->nBufferAlignment = 4;
        set_portdef(v, i, 176, 144);
    }
}

/* ---------------- callbacks & queue ---------------- */
static void event(struct venc *v, U32 ev, U32 d1, U32 d2) {
    LOG("event %u %u %u", ev, d1, d2);
    if (v->cb.EventHandler) v->cb.EventHandler(v->hcomp, v->app, ev, d1, d2, 0);
}
static void enqueue(struct venc *v, int kind, U32 a, U32 b, struct bufhdr *buf) {
    struct qitem *q = (struct qitem *)calloc(1, sizeof *q);
    if (!q) return;
    q->kind = kind; q->a = a; q->b = b; q->buf = buf;
    pthread_mutex_lock(&v->lock);
    if (v->qtail) v->qtail->next = q; else v->qhead = q;
    v->qtail = q;
    pthread_cond_signal(&v->cond);
    pthread_mutex_unlock(&v->lock);
}
static int populated(struct venc *v, int i) {
    struct port *p = &v->port[i];
    return !p->def.bEnabled || p->nbuf >= (int)p->def.nBufferCountActual;
}
static int empty_ports(struct venc *v) { return v->port[0].nbuf == 0 && v->port[1].nbuf == 0; }
/* a state change waiting for the client's buffers completes when they are all there (Idle) or all gone (Loaded) */
static void check_pending(struct venc *v) {
    if (v->pending == ST_IDLE && v->state == ST_LOADED && populated(v, 0) && populated(v, 1)) {
        v->state = ST_IDLE; v->pending = -1; event(v, EV_CMD_COMPLETE, CMD_STATE, ST_IDLE);
    } else if (v->pending == ST_LOADED && v->state == ST_IDLE && empty_ports(v)) {
        v->state = ST_LOADED; v->pending = -1; event(v, EV_CMD_COMPLETE, CMD_STATE, ST_LOADED);
    }
}
static void return_all(struct venc *v, int port) {
    if (port != 1) { for (int i = 0; i < v->nin; i++) { v->in[i]->nFilledLen = 0; if (v->cb.EmptyBufferDone) v->cb.EmptyBufferDone(v->hcomp, v->app, v->in[i]); } v->nin = 0; }
    if (port != 0) { for (int i = 0; i < v->nout; i++) { v->out[i]->nFilledLen = 0; if (v->cb.FillBufferDone) v->cb.FillBufferDone(v->hcomp, v->app, v->out[i]); } v->nout = 0; }
}

/* ---------------- encoding ---------------- */
static int host_config(struct venc *v) {
    if (v->fd < 0) v->fd = host_open();
    if (v->fd < 0) { LOG("no encoder on the host (/dev/socket/aemu_venc)"); return 0; }
    struct portdef *in = &v->port[0].def, *out = &v->port[1].def;
    U32 fps = in->xFramerate ? in->xFramerate >> 16 : (out->xFramerate ? out->xFramerate >> 16 : 15);
    U32 br = out->nBitrate ? out->nBitrate : 384000;
    struct param *p = pfind(v, IDX_VIDEO_BITRATE, 1);
    if (p && p->size >= 20) { U32 t = ((U32 *)p->data)[4]; if (t) br = t; }   /* OMX_VIDEO_PARAM_BITRATETYPE.nTargetBitrate */
    U32 msg[2 + 7] = { 1, 7 * 4, (U32)v->coding, in->nFrameWidth, in->nFrameHeight, fps ? fps : 15, br, 1, COLOR_YUV420SP };
    LOG("encoder: %s %ux%u %u fps %u bit/s", v->coding == CODING_H263 ? "H.263" : "MPEG-4", in->nFrameWidth, in->nFrameHeight, fps, br);
    if (!put_all(v->fd, msg, sizeof msg)) { close(v->fd); v->fd = -1; return 0; }
    v->configured = 1;
    return 1;
}
/* an output from the host goes to the next free output buffer */
static void deliver(struct venc *v, const uint8_t *data, U32 len, int64_t ts, U32 flags) {
    if (!v->nout) return;
    struct bufhdr *o = v->out[0];
    for (int i = 1; i < v->nout; i++) v->out[i - 1] = v->out[i];
    v->nout--;
    U32 n = len > o->nAllocLen ? o->nAllocLen : len;
    if (n < len) LOG("frame of %u bytes cut to %u", len, n);
    memcpy(o->pBuffer, data, n);
    o->nOffset = 0; o->nFilledLen = n; o->nTimeStamp = ts; o->nFlags = flags | FLAG_ENDOFFRAME;
    if (v->cb.FillBufferDone) v->cb.FillBufferDone(v->hcomp, v->app, o);
}
/* one input frame: send it, then take the host's outputs until it says "done" (type 4) */
static void encode(struct venc *v, struct bufhdr *b) {
    if (!v->configured && !host_config(v)) { event(v, EV_ERROR, (U32)OMX_ErrorHardware, 0); return; }
    U32 eos = b->nFlags & FLAG_EOS;
    U32 hdr[2 + 3] = { 2, 12 + b->nFilledLen, (U32)(uint64_t)b->nTimeStamp, (U32)((uint64_t)b->nTimeStamp >> 32), eos };
    if (!put_all(v->fd, hdr, sizeof hdr) || (b->nFilledLen && !put_all(v->fd, b->pBuffer + b->nOffset, b->nFilledLen))) {
        LOG("host channel lost"); close(v->fd); v->fd = -1; v->configured = 0; event(v, EV_ERROR, (U32)OMX_ErrorHardware, 0); return;
    }
    for (;;) {
        U32 h[2];
        if (!get_all(v->fd, h, 8)) { LOG("host channel lost"); close(v->fd); v->fd = -1; v->configured = 0; return; }
        if (h[0] == 4) break;
        if (h[0] != 3 || h[1] < 12 || h[1] > (8u << 20)) { LOG("bad message %u/%u", h[0], h[1]); close(v->fd); v->fd = -1; v->configured = 0; return; }
        U32 m[3];
        if (!get_all(v->fd, m, 12)) return;
        U32 len = h[1] - 12;
        if (len > v->pendCap) { free(v->pend); v->pend = (uint8_t *)malloc(len); v->pendCap = v->pend ? len : 0; }
        if (!v->pend || !get_all(v->fd, v->pend, len)) return;
        int64_t ts = (int64_t)((uint64_t)m[0] | ((uint64_t)m[1] << 32));
        U32 fl = (m[2] & 1 ? FLAG_SYNCFRAME : 0) | (m[2] & 2 ? FLAG_CODECCONFIG : 0) | (m[2] & 4 ? FLAG_EOS : 0);
        deliver(v, v->pend, len, ts, fl);
        if (m[2] & 4) event(v, EV_BUFFER_FLAG, 1, FLAG_EOS);
    }
}

static void do_state(struct venc *v, U32 to) {
    LOG("state %d -> %u", v->state, to);
    if ((int)to == v->state) { event(v, EV_ERROR, (U32)OMX_ErrorSameState, 0); return; }
    switch (to) {
    case ST_IDLE:
        if (v->state == ST_LOADED) { v->pending = ST_IDLE; check_pending(v); return; }
        if (v->state == ST_EXECUTING || v->state == ST_PAUSE) { return_all(v, -1); v->state = ST_IDLE; break; }
        event(v, EV_ERROR, (U32)OMX_ErrorIncorrectStateTransition, 0); return;
    case ST_LOADED:
        if (v->state == ST_IDLE) {
            if (v->fd >= 0) { close(v->fd); v->fd = -1; } v->configured = 0;
            v->pending = ST_LOADED; check_pending(v); return;
        }
        if (v->state == ST_WAIT) { v->state = ST_LOADED; break; }
        event(v, EV_ERROR, (U32)OMX_ErrorIncorrectStateTransition, 0); return;
    case ST_EXECUTING:
        if (v->state == ST_IDLE || v->state == ST_PAUSE) { v->state = ST_EXECUTING; break; }
        event(v, EV_ERROR, (U32)OMX_ErrorIncorrectStateTransition, 0); return;
    case ST_PAUSE:
        if (v->state == ST_IDLE || v->state == ST_EXECUTING) { v->state = ST_PAUSE; break; }
        event(v, EV_ERROR, (U32)OMX_ErrorIncorrectStateTransition, 0); return;
    default:
        event(v, EV_ERROR, (U32)OMX_ErrorIncorrectStateTransition, 0); return;
    }
    event(v, EV_CMD_COMPLETE, CMD_STATE, to);
}

static void *worker(void *arg) {
    struct venc *v = (struct venc *)arg;
    for (;;) {
        pthread_mutex_lock(&v->lock);
        while (!v->qhead && !v->quit) pthread_cond_wait(&v->cond, &v->lock);
        if (v->quit && !v->qhead) { pthread_mutex_unlock(&v->lock); break; }
        struct qitem *q = v->qhead;
        v->qhead = q->next; if (!v->qhead) v->qtail = 0;
        pthread_mutex_unlock(&v->lock);
        switch (q->kind) {
        case Q_CMD:
            if (q->a == CMD_STATE) do_state(v, q->b);
            else if (q->a == CMD_FLUSH) {
                U32 p = q->b;
                return_all(v, p == ALL_PORTS ? -1 : (int)p);
                if (p == ALL_PORTS) { event(v, EV_CMD_COMPLETE, CMD_FLUSH, 0); event(v, EV_CMD_COMPLETE, CMD_FLUSH, 1); }
                else event(v, EV_CMD_COMPLETE, CMD_FLUSH, p);
            } else if (q->a == CMD_PORT_DISABLE || q->a == CMD_PORT_ENABLE) {
                for (U32 i = 0; i < 2; i++) if (q->b == ALL_PORTS || q->b == i) {
                    v->port[i].def.bEnabled = q->a == CMD_PORT_ENABLE;
                    if (q->a == CMD_PORT_DISABLE) return_all(v, (int)i);
                    event(v, EV_CMD_COMPLETE, q->a, i);
                }
            } else if (q->a == CMD_MARK) event(v, EV_CMD_COMPLETE, CMD_MARK, q->b);
            else if (q->a == 0xff) check_pending(v);   /* a buffer came or went */
            break;
        case Q_ETB:
            if (v->nin < MAXBUF) v->in[v->nin++] = q->buf;
            break;
        case Q_FTB:
            if (v->nout < MAXBUF) v->out[v->nout++] = q->buf;
            break;
        }
        free(q);
        /* encode while there is input and somewhere to put what comes out */
        while (v->state == ST_EXECUTING && v->nin && v->nout) {
            struct bufhdr *b = v->in[0];
            for (int i = 1; i < v->nin; i++) v->in[i - 1] = v->in[i];
            v->nin--;
            encode(v, b);
            b->nFilledLen = 0;
            if (v->cb.EmptyBufferDone) v->cb.EmptyBufferDone(v->hcomp, v->app, b);
        }
    }
    return 0;
}

/* ---------------- the virtual methods ---------------- */
static void dtor(struct venc *v) {
    if (v->thread) {
        pthread_mutex_lock(&v->lock); v->quit = 1; pthread_cond_signal(&v->cond); pthread_mutex_unlock(&v->lock);
        pthread_join(v->thread, 0); v->thread = 0;
    }
    if (v->fd >= 0) close(v->fd);
    for (int i = 0; i < v->nparams; i++) free(v->params[i].data);
    free(v->pend);
}
static void dtor0(struct venc *v) { dtor(v); free(v); }

static OMX_ERR m_init(struct venc *v, const char *name) {
    LOG("component_init %s", name ? name : "?");
    if (!name) return OMX_ErrorBadParameter;
    if (!strcmp(name, "OMX.qcom.video.encoder.mpeg4")) { v->coding = CODING_MPEG4; strncpy(v->role, "video_encoder.mpeg4", sizeof v->role); }
    else if (!strcmp(name, "OMX.qcom.video.encoder.h263")) { v->coding = CODING_H263; strncpy(v->role, "video_encoder.h263", sizeof v->role); }
    else return OMX_ErrorBadParameter;
    v->hcomp = &v->cmp[0];
    v->state = ST_LOADED; v->pending = -1; v->fd = -1;
    init_ports(v);
    pthread_mutex_init(&v->lock, 0); pthread_cond_init(&v->cond, 0);
    if (pthread_create(&v->thread, 0, worker, v) != 0) { v->thread = 0; return OMX_ErrorInsufficientResources; }
    return OMX_ErrorNone;
}
static OMX_ERR m_version(struct venc *v, void *h, char *name, U32 *cv, U32 *sv, void *uuid) {
    (void)h; (void)uuid;
    if (name) strncpy(name, v->coding == CODING_H263 ? "OMX.qcom.video.encoder.h263" : "OMX.qcom.video.encoder.mpeg4", 128);
    if (cv) *cv = 0x00000101; if (sv) *sv = 0x00000101;
    return OMX_ErrorNone;
}
static OMX_ERR m_command(struct venc *v, void *h, U32 cmd, U32 p1, void *data) {
    (void)h; (void)data;
    LOG("send_command %u %u", cmd, p1);
    if (v->state == ST_INVALID) return OMX_ErrorInvalidState;
    if (cmd > CMD_MARK) return OMX_ErrorBadParameter;
    enqueue(v, Q_CMD, cmd, p1, 0);
    return OMX_ErrorNone;
}
static OMX_ERR m_getparam(struct venc *v, void *h, U32 idx, void *data) {
    (void)h;
    if (!data) return OMX_ErrorBadParameter;
    U32 *d = (U32 *)data;
    switch (idx) {
    case IDX_PARAM_PORTDEF: {
        U32 p = d[2];
        if (p > 1) return OMX_ErrorBadPortIndex;
        U32 n = d[0] < sizeof(struct portdef) ? d[0] : sizeof(struct portdef);
        memcpy(data, &v->port[p].def, n); d[0] = n;
        return OMX_ErrorNone;
    }
    case IDX_PARAM_VIDEO_INIT:   /* OMX_PORT_PARAM_TYPE: nSize, nVersion, nPorts, nStartPortNumber */
        d[2] = 2; d[3] = 0; return OMX_ErrorNone;
    case IDX_PARAM_AUDIO_INIT: case IDX_PARAM_IMAGE_INIT: case IDX_PARAM_OTHER_INIT:
        d[2] = 0; d[3] = 0; return OMX_ErrorNone;
    case IDX_PARAM_ROLE:         /* OMX_PARAM_COMPONENTROLETYPE: nSize, nVersion, cRole[128] */
        strncpy((char *)&d[2], v->role, 128); return OMX_ErrorNone;
    case IDX_VIDEO_PORTFORMAT: { /* OMX_VIDEO_PARAM_PORTFORMATTYPE: …, nPortIndex, nIndex, eCompressionFormat, eColorFormat, xFramerate */
        U32 p = d[2];
        if (p > 1) return OMX_ErrorBadPortIndex;
        if (d[3] > 0) return OMX_ErrorNoMore;
        d[4] = v->port[p].def.eCompressionFormat; d[5] = v->port[p].def.eColorFormat; d[6] = v->port[p].def.xFramerate;
        return OMX_ErrorNone;
    }
    }
    /* codec parameters: what the client set, or the defaults of OMX_Video.h */
    U32 port = d[0] >= 12 ? d[2] : 0;
    struct param *pp = pfind(v, idx, port);
    if (pp) { U32 n = d[0] < pp->size ? d[0] : pp->size; memcpy(data, pp->data, n); return OMX_ErrorNone; }
    switch (idx) {
    case IDX_VIDEO_BITRATE:      /* eControlRate (variable 1), nTargetBitrate */
        d[3] = 1; d[4] = v->port[1].def.nBitrate; return OMX_ErrorNone;
    case IDX_VIDEO_QUANT:        /* nQpI, nQpP, nQpB */
        d[3] = 10; d[4] = 12; d[5] = 0; return OMX_ErrorNone;
    case IDX_VIDEO_MPEG4: case IDX_VIDEO_H263: case IDX_VIDEO_ERRORCORR: case IDX_VIDEO_MOTION:
    case IDX_VIDEO_INTRAREFRESH: case IDX_VIDEO_PROFILE_CUR:
        memset(d + 3, 0, d[0] > 12 ? d[0] - 12 : 0); return OMX_ErrorNone;
    }
    LOG("get_parameter 0x%x: unsupported", idx);
    return OMX_ErrorUnsupportedIndex;
}
static OMX_ERR m_setparam(struct venc *v, void *h, U32 idx, void *data) {
    (void)h;
    if (!data) return OMX_ErrorBadParameter;
    U32 *d = (U32 *)data;
    if (idx == IDX_PARAM_PORTDEF) {
        struct portdef *s = (struct portdef *)data;
        if (s->nPortIndex > 1) return OMX_ErrorBadPortIndex;
        struct portdef *p = &v->port[s->nPortIndex].def;
        LOG("port %u: %ux%u, %u buffers, %u fps (Q16 %x), %u bit/s, color %x", s->nPortIndex, s->nFrameWidth, s->nFrameHeight,
            s->nBufferCountActual, s->xFramerate >> 16, s->xFramerate, s->nBitrate, s->eColorFormat);
        if (s->nBufferCountActual >= p->nBufferCountMin && s->nBufferCountActual <= MAXBUF) p->nBufferCountActual = s->nBufferCountActual;
        if (s->nFrameWidth && s->nFrameHeight) {
            set_portdef(v, (int)s->nPortIndex, s->nFrameWidth, s->nFrameHeight);
            if (s->nPortIndex == 0) set_portdef(v, 1, s->nFrameWidth, s->nFrameHeight);   /* the output follows the input */
        }
        if (s->xFramerate) p->xFramerate = s->xFramerate;
        if (s->nPortIndex == 1 && s->nBitrate) p->nBitrate = s->nBitrate;
        if (s->nPortIndex == 0 && s->eColorFormat && s->eColorFormat != COLOR_YUV420SP) LOG("input color %x: frames are taken as YUV420SP", s->eColorFormat);
        return OMX_ErrorNone;
    }
    if (idx == IDX_PARAM_ROLE) {
        const char *r = (const char *)&d[2];
        if (!strcmp(r, "video_encoder.mpeg4")) v->coding = CODING_MPEG4;
        else if (!strcmp(r, "video_encoder.h263")) v->coding = CODING_H263;
        else return OMX_ErrorBadParameter;
        strncpy(v->role, r, sizeof v->role);
        v->port[1].def.eCompressionFormat = (U32)v->coding;
        return OMX_ErrorNone;
    }
    if (idx == IDX_VIDEO_PORTFORMAT) {
        if (d[2] > 1) return OMX_ErrorBadPortIndex;
        if (d[2] == 0 && d[5] && d[5] != COLOR_YUV420SP) LOG("input color %x requested", d[5]);
        if (d[6]) v->port[d[2]].def.xFramerate = d[6];
        return OMX_ErrorNone;
    }
    if (d[0] < 12 || d[0] > 4096) return OMX_ErrorBadParameter;
    LOG("set_parameter 0x%x (%u bytes) kept", idx, d[0]);
    pstore(v, idx, d[2], data, d[0]);
    if (idx == IDX_VIDEO_BITRATE && d[4]) v->port[1].def.nBitrate = d[4];
    return OMX_ErrorNone;
}
static OMX_ERR m_getconfig(struct venc *v, void *h, U32 idx, void *data) {
    (void)v; (void)h; (void)data;
    LOG("get_config 0x%x: unsupported", idx);
    return OMX_ErrorUnsupportedIndex;
}
static OMX_ERR m_setconfig(struct venc *v, void *h, U32 idx, void *data) {
    (void)v; (void)h; (void)data;
    LOG("set_config 0x%x accepted", idx);
    return OMX_ErrorNone;
}
static OMX_ERR m_extindex(struct venc *v, void *h, const char *name, U32 *idx) {
    (void)v; (void)h; (void)idx;
    LOG("get_extension_index %s: none", name ? name : "?");
    return OMX_ErrorUnsupportedIndex;
}
static OMX_ERR m_getstate(struct venc *v, void *h, U32 *st) { (void)h; if (!st) return OMX_ErrorBadParameter; *st = (U32)v->state; return OMX_ErrorNone; }
static OMX_ERR m_tunnel(void) { return OMX_ErrorNotImplemented; }
static OMX_ERR add_buffer(struct venc *v, struct bufhdr **out, U32 port, void *app, U32 bytes, uint8_t *mem, int own) {
    if (port > 1) return OMX_ErrorBadPortIndex;
    struct port *p = &v->port[port];
    if (p->nbuf >= MAXBUF) return OMX_ErrorInsufficientResources;
    struct bufhdr *b = (struct bufhdr *)calloc(1, sizeof *b);
    if (!b) return OMX_ErrorInsufficientResources;
    if (!mem) { mem = (uint8_t *)malloc(bytes ? bytes : 1); if (!mem) { free(b); return OMX_ErrorInsufficientResources; } }
    b->nSize = sizeof *b; b->nVersion = 0x00000101; b->pBuffer = mem; b->nAllocLen = bytes; b->pAppPrivate = app;
    if (port == 0) { b->nInputPortIndex = 0; b->nOutputPortIndex = ALL_PORTS; } else { b->nOutputPortIndex = 1; b->nInputPortIndex = ALL_PORTS; }
    p->buf[p->nbuf] = b; p->own[p->nbuf] = (uint8_t)own; p->nbuf++;
    *out = b;
    p->def.bPopulated = populated(v, (int)port);
    enqueue(v, Q_CMD, 0xff, 0, 0);   /* the worker looks at a waiting state change */
    return OMX_ErrorNone;
}
static OMX_ERR m_usebuf(struct venc *v, void *h, struct bufhdr **out, U32 port, void *app, U32 bytes, uint8_t *mem) {
    (void)h;
    if (!out || !mem) return OMX_ErrorBadParameter;
    return add_buffer(v, out, port, app, bytes, mem, 0);
}
static OMX_ERR m_allocbuf(struct venc *v, void *h, struct bufhdr **out, U32 port, void *app, U32 bytes) {
    (void)h;
    if (!out) return OMX_ErrorBadParameter;
    return add_buffer(v, out, port, app, bytes, 0, 1);
}
static OMX_ERR m_freebuf(struct venc *v, void *h, U32 port, struct bufhdr *b) {
    (void)h;
    if (port > 1 || !b) return OMX_ErrorBadPortIndex;
    struct port *p = &v->port[port];
    for (int i = 0; i < p->nbuf; i++) if (p->buf[i] == b) {
        if (p->own[i]) free(b->pBuffer);
        free(b);
        for (int k = i + 1; k < p->nbuf; k++) { p->buf[k - 1] = p->buf[k]; p->own[k - 1] = p->own[k]; }
        p->nbuf--;
        p->def.bPopulated = 0;
        enqueue(v, Q_CMD, 0xff, 0, 0);
        return OMX_ErrorNone;
    }
    return OMX_ErrorBadParameter;
}
static OMX_ERR m_etb(struct venc *v, void *h, struct bufhdr *b) {
    (void)h;
    if (!b) return OMX_ErrorBadParameter;
    if (v->state != ST_EXECUTING && v->state != ST_PAUSE) return OMX_ErrorIncorrectStateOperation;
    enqueue(v, Q_ETB, 0, 0, b);
    return OMX_ErrorNone;
}
static OMX_ERR m_ftb(struct venc *v, void *h, struct bufhdr *b) {
    (void)h;
    if (!b) return OMX_ErrorBadParameter;
    if (v->state != ST_EXECUTING && v->state != ST_PAUSE) return OMX_ErrorIncorrectStateOperation;
    enqueue(v, Q_FTB, 0, 0, b);
    return OMX_ErrorNone;
}
static OMX_ERR m_callbacks(struct venc *v, void *h, struct callbacks *cb, void *app) {
    (void)h;
    if (!cb) return OMX_ErrorBadParameter;
    v->cb = *cb; v->app = app;
    return OMX_ErrorNone;
}
static OMX_ERR m_deinit(struct venc *v, void *h) { (void)v; (void)h; LOG("component_deinit"); return OMX_ErrorNone; }
static OMX_ERR m_egl(void) { return OMX_ErrorNotImplemented; }
static OMX_ERR m_roleenum(struct venc *v, void *h, char *role, U32 index) {
    (void)h;
    if (index > 0) return OMX_ErrorNoMore;
    if (role) strncpy(role, v->role, 128);
    return OMX_ErrorNone;
}

EXPORT void *get_omx_component_factory_fn(void) {
    if (!g_vt[0]) {
        void *t[21] = { (void *)dtor, (void *)dtor0, (void *)m_init, (void *)m_version, (void *)m_command, (void *)m_getparam,
            (void *)m_setparam, (void *)m_getconfig, (void *)m_setconfig, (void *)m_extindex, (void *)m_getstate,
            (void *)m_tunnel, (void *)m_usebuf, (void *)m_allocbuf, (void *)m_freebuf, (void *)m_etb, (void *)m_ftb,
            (void *)m_callbacks, (void *)m_deinit, (void *)m_egl, (void *)m_roleenum };
        memcpy(g_vt, t, sizeof t);
    }
    struct venc *v = (struct venc *)calloc(1, sizeof *v);
    if (!v) return 0;
    v->vptr = g_vt;
    v->fd = -1;
    return v;
}
