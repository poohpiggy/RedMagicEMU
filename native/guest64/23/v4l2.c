/*
 * V4L2 capture devices for the 32-bit guest: /dev/video0 (the camera) and /dev/video2 (its recording path).
 *
 * Vendor camera HALs of 2.3 (Samsung SecCameraHardware::FimcV4l2 of the Galaxy S line) drive the kernel's FIMC
 * driver directly: open, VIDIOC_QUERYCAP (capture bit required), ENUMINPUT/S_INPUT (the sensor), ENUM_FMT until
 * the wanted fourcc, S_FMT, REQBUFS (MMAP), QUERYBUF + mmap, QBUF, STREAMON, poll, DQBUF, S_CTRL/G_CTRL with the
 * vendor's private controls. The calls and their order are those of FimcV4l2::init/start/qbuf/dqbuf in the
 * firmware's libcamera.so (seen by disassembly); without /dev/video0 init fails and CameraService has no camera.
 *
 * Frames come from the phone's camera through the app's camera server, <root>/dev/socket/aemu_cam (CameraFeed:
 * "open <n>", "start", "stop", "shot"; answers int32 type, w, h, len + data: type 1 RGBA then NV21 640x480,
 * type 2 JPEG). DQBUF takes the next frame and writes it into the oldest queued buffer, scaled and converted to the
 * format S_FMT chose. The buffers live in a memfd, so the HAL's MemoryHeapBase on this fd and the mmap of a
 * buffer's offset both reach them, and the heap can be handed to SurfaceFlinger over binder.
 *
 * Layouts: uapi linux/videodev2.h (NDK r28c sysroot) for 32-bit ARM, sizes checked against the ioctl numbers the
 * HAL uses: capability 104, input 80, fmtdesc 64, format 204, requestbuffers 20, streamparm 204, buffer 68,
 * ext_controls 24, ext_control 20. Arguments are guest pointers (G() adds qemu's guest base).
 */

#define V4_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define V4_ALIGN 0x10000u              /* buffers on 64 KB: any host page size, as for ALSA */
#define V4_MAXBUF 8

/* the formats the Samsung HAL may ask for (its table: JPEG, NV21, YU12, YUYV, NV61, RGBP) */
static const u32 v4_fmts[] = {
    V4_FOURCC('N', 'V', '2', '1'), V4_FOURCC('Y', 'U', '1', '2'), V4_FOURCC('Y', 'U', 'Y', 'V'),
    V4_FOURCC('N', 'V', '6', '1'), V4_FOURCC('R', 'G', 'B', 'P'), V4_FOURCC('J', 'P', 'E', 'G'),
};

struct v4_st {
    u64 ino, dev;
    int used, input, cam, streaming;
    u32 w, h, fourcc, size, nbuf, seq, jlen, done;   /* jlen: the last JPEG; done: index + 1 of a picture kept past STREAMOFF */
    u8 queued[V4_MAXBUF];
    u32 qorder[V4_MAXBUF], qhead, qtail;   /* queued buffer indexes in order */
    u8 *map; u64 mapsz;
};
static struct v4_st g_v4[4];
static u8 *g_v4frame; static u64 g_v4framesz;

static struct v4_st *v4_of(int fd) {
    struct kstat st;
    if (fd < 0 || S2(NR_fstat, fd, &st) != 0) return 0;
    for (int i = 0; i < 4; i++) if (g_v4[i].used && g_v4[i].ino == st.st_ino && g_v4[i].dev == st.st_dev) return &g_v4[i];
    return 0;
}

static long v4_open(const char *p, long flags) {
    if (!seq(p, "/dev/video0") && !seq(p, "/dev/video2")) return -ENOENT;
    struct v4_st *s = 0;
    for (int i = 0; i < 4 && !s; i++) if (!g_v4[i].used) s = &g_v4[i];
    if (!s) return -16;                /* EBUSY */
    long fd = S2(NR_memfd_create, "aemu-video", (flags & 02000000) ? 1 : 0);
    if (fd < 0) return fd;
    struct kstat st; S2(NR_fstat, fd, &st);
    memset(s, 0, sizeof *s);
    s->ino = st.st_ino; s->dev = st.st_dev; s->used = 1; s->cam = -1;
    s->w = 640; s->h = 480; s->fourcc = v4_fmts[0]; s->size = 640 * 480 * 3 / 2;
    return fd;
}

static void v4_unmap(struct v4_st *s) { if (s->map) S2(215 /*munmap*/, s->map, s->mapsz); s->map = 0; s->mapsz = 0; }

static void v4_closed(int fd) {
    struct v4_st *s = v4_of(fd);
    if (!s) return;
    S1(NR_close, fd);
    for (int f = 0; f < 1024; f++) {
        struct kstat st;
        if (S2(NR_fstat, f, &st) == 0 && st.st_ino == s->ino && st.st_dev == s->dev) return;
    }
    if (s->cam >= 0) { S3(NR_write, s->cam, "close\n", 6); S1(NR_close, s->cam); }
    v4_unmap(s);
    s->used = 0;
}

static u32 v4_sizeimage(u32 fourcc, u32 w, u32 h) {
    if (fourcc == v4_fmts[0] || fourcc == v4_fmts[1]) return w * h * 3 / 2;
    if (fourcc == v4_fmts[5]) return w * h * 2 + 0x10000;   /* JPEG: room for a picture of the size */
    return w * h * 2;
}

/* ---- the phone's camera ---- */
static int v4_send(struct v4_st *s, const char *t) { return S3(NR_write, s->cam, t, slen(t)) == (long)slen(t); }

static int v4_connect(struct v4_st *s) {
    if (s->cam >= 0) return 1;
    struct sockaddr_un sa; sa.family = 1;
    static const char P[] = "/dev/socket/aemu_cam";
    if (g_rootlen + sizeof P > sizeof sa.path) return 0;
    memcpy(sa.path, g_root, g_rootlen); memcpy(sa.path + g_rootlen, P, sizeof P);
    long c = S3(NR_socket, 1, 1 | 02000000, 0);
    if (c < 0) return 0;
    if (S3(NR_connect, c, &sa, 2 + g_rootlen + sizeof P) < 0) { S1(NR_close, c); return 0; }
    s->cam = (int)c;
    char cmd[16] = "open 0\n"; cmd[5] = (char)('0' + (s->input & 1));
    return v4_send(s, cmd);
}

static int v4_readn(int fd, u8 *p, u64 n) {
    while (n) { long r = S3(NR_read, fd, p, n); if (r == -4) continue; if (r <= 0) return 0; p += r; n -= (u64)r; }
    return 1;
}

/* the next frame of the wanted type into g_v4frame; its size and dimensions */
static long v4_frame(struct v4_st *s, int type, u32 *fw, u32 *fh) {
    for (;;) {
        int hd[4];
        if (!v4_readn(s->cam, (u8 *)hd, sizeof hd)) return -5;   /* EIO */
        u64 len = (u64)(u32)hd[3];
        if (len > g_v4framesz) {
            if (g_v4frame) S2(215, g_v4frame, g_v4framesz);
            g_v4framesz = (len + 0xffff) & ~0xffffUL;
            long m = S6(NR_mmap, 0, g_v4framesz, 3, 0x22, -1, 0);
            if (m < 0 && m > -4096) { g_v4frame = 0; g_v4framesz = 0; return -12; }
            g_v4frame = (u8 *)m;
        }
        if (!v4_readn(s->cam, g_v4frame, len)) return -5;
        if (hd[0] == type) { *fw = (u32)hd[1]; *fh = (u32)hd[2]; return (long)len; }
    }
}

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* NV21 sw x sh (src) -> the buffer's format at w x h, nearest sample */
static void v4_convert(const u8 *src, u32 sw, u32 sh, u8 *dst, u32 fourcc, u32 w, u32 h) {
    const u8 *sy = src, *svu = src + (u64)sw * sh;
    for (u32 y = 0; y < h; y++) {
        u32 yy = y * sh / h;
        const u8 *ry = sy + (u64)yy * sw, *rc = svu + (u64)(yy / 2) * sw;
        for (u32 x = 0; x < w; x++) {
            u32 xx = x * sw / w, cx = xx & ~1u;
            u8 Y = ry[xx], V = rc[cx], U = rc[cx + 1];
            if (fourcc == v4_fmts[2]) {                                   /* YUYV */
                u8 *o = dst + ((u64)y * w + x) * 2; o[0] = Y; o[1] = (x & 1) ? V : U;
            } else if (fourcc == v4_fmts[4]) {                            /* RGB565 */
                int c = Y - 16, d = U - 128, e = V - 128;
                int r = clamp255((298 * c + 409 * e + 128) >> 8), g = clamp255((298 * c - 100 * d - 208 * e + 128) >> 8),
                    b = clamp255((298 * c + 516 * d + 128) >> 8);
                u16 px = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                memcpy(dst + ((u64)y * w + x) * 2, &px, 2);
            } else {
                dst[(u64)y * w + x] = Y;
                if (fourcc == v4_fmts[3]) {                               /* NV61: VU on every line */
                    if (!(x & 1)) { u8 *o = dst + (u64)w * h + (u64)y * w + x; o[0] = V; o[1] = U; }
                } else if (!(y & 1) && !(x & 1)) {
                    if (fourcc == v4_fmts[0]) { u8 *o = dst + (u64)w * h + (u64)(y / 2) * w + x; o[0] = V; o[1] = U; }
                    else {                                                /* YU12: U plane, then V */
                        u64 q = (u64)(y / 2) * (w / 2) + x / 2;
                        dst[(u64)w * h + q] = U; dst[(u64)w * h + (u64)(w / 2) * (h / 2) + q] = V;
                    }
                }
            }
        }
    }
}

static long v4_reqbufs(struct v4_st *s, int fd, u32 count) {
    if (count > V4_MAXBUF) count = V4_MAXBUF;
    u64 slot = ((u64)s->size + V4_ALIGN - 1) & ~(u64)(V4_ALIGN - 1);
    v4_unmap(s);
    s->nbuf = count; s->qhead = s->qtail = 0;
    memset(s->queued, 0, sizeof s->queued);
    S2(46 /*ftruncate*/, fd, count ? slot * count : 0);
    if (count) {
        long m = S6(NR_mmap, 0, slot * count, 3, 1 /*MAP_SHARED*/, fd, 0);
        if (m < 0 && m > -4096) return -12;
        s->map = (u8 *)m; s->mapsz = slot * count;
    }
    return 0;
}

static void v4_fillbuf(struct v4_st *s, u8 *b, u32 i, u32 used) {     /* struct v4l2_buffer, 68 bytes */
    u64 slot = ((u64)s->size + V4_ALIGN - 1) & ~(u64)(V4_ALIGN - 1);
    memset(b, 0, 68);
    *(u32 *)b = i; *(u32 *)(b + 4) = 1; *(u32 *)(b + 8) = used;
    *(u32 *)(b + 12) = 1 /*MAPPED*/ | (s->queued[i] ? 2 : 0);
    *(u32 *)(b + 16) = 1 /*FIELD_NONE*/;
    struct { long sec, nsec; } now; S2(113, 1, &now);
    *(u32 *)(b + 20) = (u32)now.sec; *(u32 *)(b + 24) = (u32)(now.nsec / 1000);
    *(u32 *)(b + 44) = s->seq;
    *(u32 *)(b + 48) = 1 /*MMAP*/; *(u32 *)(b + 52) = (u32)(slot * i); *(u32 *)(b + 56) = s->size;
}

/* the next frame into the oldest queued buffer; its index, or a negative error */
static long v4_capture(struct v4_st *s, u32 *usedp) {
    if (s->qhead == s->qtail) return -22;
    u32 i = s->qorder[s->qhead % V4_MAXBUF];
    u64 slot = ((u64)s->size + V4_ALIGN - 1) & ~(u64)(V4_ALIGN - 1);
    u8 *dst = s->map + slot * i;
    u32 fw = 0, fh = 0, used = s->size;
    if (s->fourcc == v4_fmts[5]) {
        if (!v4_send(s, "shot\n")) return -5;
        long n = v4_frame(s, 2, &fw, &fh);
        if (n < 0) return n;
        used = (u32)n < s->size ? (u32)n : s->size;
        memcpy(dst, g_v4frame, used);
        s->jlen = used;
    } else {
        long n = v4_frame(s, 1, &fw, &fh);
        if (n < 0) return n;
        if (!fw || !fh) return -5;
        v4_convert(g_v4frame + (u64)fw * fh * 4, fw, fh, dst, s->fourcc, s->w, s->h);
    }
    s->qhead++; s->queued[i] = 0; s->seq++;
    *usedp = used;
    return i;
}

static long v4_dqbuf(struct v4_st *s, u8 *b) {
    u32 used = s->size;
    long i;
    if (s->done) { i = s->done - 1; s->done = 0; used = s->fourcc == v4_fmts[5] ? s->jlen : s->size; }
    else if (!s->streaming) return -22;
    else if ((i = v4_capture(s, &used)) < 0) return i;
    v4_fillbuf(s, b, (u32)i, used);
    return 0;
}

/* buffer <i> as the frame of overlay <id>: to the app, which draws it in the overlay's place over the screen
 * (<root>/dev/socket/aemu_ovl: u32 2, id, w, h, fourcc, length, then the bytes) */
static int g_ovl = -1;
static long v4_show(struct v4_st *s, u32 id, u32 i) {
    if (i >= s->nbuf || !s->map) return -22;
    if (g_ovl < 0) {
        struct sockaddr_un sa; sa.family = 1;
        static const char P[] = "/dev/socket/aemu_ovl";
        if (g_rootlen + sizeof P > sizeof sa.path) return -5;
        memcpy(sa.path, g_root, g_rootlen); memcpy(sa.path + g_rootlen, P, sizeof P);
        long c = S3(NR_socket, 1, 1 | 02000000, 0);
        if (c < 0) return -5;
        if (S3(NR_connect, c, &sa, 2 + g_rootlen + sizeof P) < 0) { S1(NR_close, c); return -5; }
        g_ovl = (int)c;
    }
    u64 slot = ((u64)s->size + V4_ALIGN - 1) & ~(u64)(V4_ALIGN - 1);
    u32 h[6] = { 2, id, s->w, s->h, s->fourcc, s->size };
    const u8 *d = s->map + slot * i;
    u64 left = s->size; long r = S3(NR_write, g_ovl, h, sizeof h);
    while (r > 0 && left) { r = S3(NR_write, g_ovl, d, left); if (r == -4) { r = 1; continue; } if (r > 0) { d += r; left -= (u64)r; } }
    if (r <= 0) { S1(NR_close, g_ovl); g_ovl = -1; return -5; }
    return 0;
}

static long v4_ioctl(int fd, u32 cmd, u64 arg) {
    struct v4_st *s = v4_of(fd);
    if (!s) return -25;
    u8 *p = (u8 *)G(arg);
    switch (cmd) {
    case 0x80685600: {                                                  /* QUERYCAP */
        memset(p, 0, 104);
        memcpy(p, "aemu-fimc", 9); memcpy(p + 16, "AEmulator camera", 16); memcpy(p + 48, "platform", 8);
        *(u32 *)(p + 80) = 0x020620;                                    /* 2.6.32 */
        *(u32 *)(p + 84) = 1 /*VIDEO_CAPTURE*/ | 0x04000000 /*STREAMING*/;
        return 0;
    }
    case 0xc050561a: {                                                  /* ENUMINPUT */
        u32 i = *(u32 *)p;
        if (i > 1) return -22;
        memset(p + 4, 0, 76);
        memcpy(p + 4, i ? "front camera" : "back camera", i ? 12 : 11);
        *(u32 *)(p + 36) = 2;                                           /* INPUT_TYPE_CAMERA */
        return 0;
    }
    case 0xc0045627: {                                                  /* S_INPUT */
        u32 i = *(u32 *)p;
        if (i > 1) return -22;
        s->input = (int)i;
        return 0;
    }
    case 0x80045626: *(u32 *)p = (u32)s->input; return 0;              /* G_INPUT */
    case 0xc0405602: {                                                  /* ENUM_FMT */
        u32 i = *(u32 *)p;
        if (i >= sizeof v4_fmts / sizeof v4_fmts[0]) return -22;
        memset(p + 8, 0, 56);
        *(u32 *)(p + 8) = v4_fmts[i] == v4_fmts[5] ? 1 /*COMPRESSED*/ : 0;
        memcpy(p + 12, &v4_fmts[i], 4);
        *(u32 *)(p + 44) = v4_fmts[i];
        return 0;
    }
    case 0xc0cc5605: case 0xc0cc5640: case 0xc0cc5604: {                /* S_FMT, TRY_FMT, G_FMT */
        u32 *f = (u32 *)(p + 4);
        if ((cmd & 0xff) != 0x04) {
            u32 four = f[2], ok = 0;
            for (u64 k = 0; k < sizeof v4_fmts / sizeof v4_fmts[0]; k++) ok |= v4_fmts[k] == four;
            if (!ok || !f[0] || !f[1] || f[0] > 4096 || f[1] > 4096) return -22;
            if ((cmd & 0xff) == 0x05) {
                if (s->streaming) return -16;
                s->w = f[0] & ~1u; s->h = f[1] & ~1u; s->fourcc = four; s->size = v4_sizeimage(four, s->w, s->h);
            }
        } else { f[0] = s->w; f[1] = s->h; f[2] = s->fourcc; }
        u32 w = f[0] & ~1u, h = f[1] & ~1u;
        f[0] = w; f[1] = h; f[3] = 1;
        f[4] = f[2] == v4_fmts[2] || f[2] == v4_fmts[4] ? w * 2 : f[2] == v4_fmts[5] ? 0 : w;
        f[5] = v4_sizeimage(f[2], w, h);
        f[6] = f[2] == v4_fmts[5] ? 7 /*JPEG*/ : 1 /*SMPTE170M*/;
        return 0;
    }
    case 0xc0145608: {                                                  /* REQBUFS */
        if (*(u32 *)(p + 8) != 1) return -22;                           /* MMAP only */
        if (s->streaming) return -16;
        long r = v4_reqbufs(s, fd, *(u32 *)p);
        if (r < 0) return r;
        *(u32 *)p = s->nbuf;
        return 0;
    }
    case 0xc0445609: {                                                  /* QUERYBUF */
        u32 i = *(u32 *)p;
        if (i >= s->nbuf) return -22;
        v4_fillbuf(s, p, i, 0);
        return 0;
    }
    case 0xc044560f: {                                                  /* QBUF */
        u32 i = *(u32 *)p;
        if (i >= s->nbuf || s->queued[i]) return -22;
        s->queued[i] = 1; s->qorder[s->qtail++ % V4_MAXBUF] = i;
        *(u32 *)(p + 12) |= 2;
        return 0;
    }
    case 0xc0445611: return v4_dqbuf(s, p);                            /* DQBUF */
    case 0x40045612:                                                    /* STREAMON */
        if (!s->nbuf) return -22;
        if (!v4_connect(s) || !v4_send(s, "start\n")) return -5;
        s->streaming = 1;
        return 0;
    case 0x40045613:                                                    /* STREAMOFF: the queue is emptied */
        /* Samsung's snapshot (SecCameraHardware::nativeGetSnapshot): poll, STREAMOFF, then DQBUF of the picture; the
         * FIMC driver keeps the captured picture past STREAMOFF, so it is taken here and handed to that DQBUF */
        s->done = 0;
        if (s->streaming && s->fourcc == v4_fmts[5]) { u32 u; long i = v4_capture(s, &u); if (i >= 0) s->done = (u32)i + 1; }
        if (s->cam >= 0) v4_send(s, "stop\n");
        s->streaming = 0; s->qhead = s->qtail = 0;
        memset(s->queued, 0, sizeof s->queued);
        return 0;
    case 0xc0cc5616: case 0xc0cc5615: {                                 /* S_PARM, G_PARM: 30 fps */
        memset(p + 4, 0, 200);
        *(u32 *)(p + 4) = 0x1000;                                       /* CAP_TIMEPERFRAME */
        *(u32 *)(p + 12) = 1; *(u32 *)(p + 16) = 30;
        return 0;
    }
    /* controls: the vendor's private ones (zoom, effects, scene, the physical address of a frame...) are kept as
     * set; nothing here acts on them, a read gives what was written last or 0 */
    case 0xc008561c: {                                                  /* S_CTRL */
        u32 id = *(u32 *)p, v = *(u32 *)(p + 4);
        /* the "physical address" of buffer <v>'s Y (PRIVATE_BASE + 1) or CbCr (+ 4) plane: Samsung's HAL hands it to
         * the overlay; ours (native/overlayhal) turns it back into this fd and buffer */
        if ((id == 0x08000001 || id == 0x08000004) && v < s->nbuf)
            *(u32 *)(p + 4) = 0xa0000000u | ((u32)(fd & 0xfff) << 16) | (v << 8) | (id == 0x08000004);
        return 0;
    }
    case 0x400856f0: return v4_show(s, ((u32 *)p)[0], ((u32 *)p)[1]);   /* AEMU_OVL_SHOW (overlay_hal.c) */
    case 0xc008561b: {                                                  /* G_CTRL */
        /* Samsung's JPEG controls (videodev2_samsung.h of crespo, AOSP 2.3: PRIVATE_BASE + 32..36), read by
         * SecCameraHardware::getJpeg after the snapshot DQBUF: the picture lies at the start of the buffer, no
         * thumbnail; the postview follows the picture */
        u32 id = *(u32 *)p, v = 0;
        if (id == 0x08000020) v = s->jlen;                                 /* JPEG_MAIN_SIZE */
        else if (id == 0x08000024) v = (s->jlen + 0xfff) & ~0xfffu;        /* JPEG_POSTVIEW_OFFSET */
        *(u32 *)(p + 4) = v;
        return 0;
    }
    case 0xc0185647: case 0xc0185648: {                                 /* G/S_EXT_CTRLS */
        u32 n = *(u32 *)(p + 4);
        u8 *c = (u8 *)G(*(u32 *)(p + 20));
        if ((cmd & 0xff) == 0x47) for (u32 k = 0; k < n && k < 64; k++) {
            u8 *e = c + 20 * k;                                         /* id, size, reserved2, value / pointer */
            u32 sz = *(u32 *)(e + 4);
            if (sz) { u8 *str = (u8 *)G(*(u32 *)(e + 12)); memset(str, 0, sz); }
            else memset(e + 12, 0, 8);
        }
        return 0;
    }
    }
    return -25;                                                         /* ENOTTY */
}
