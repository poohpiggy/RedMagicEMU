/*
 * ALSA for the 32-bit guest: /dev/snd/controlC0, pcmC0D0p, pcmC0D0c.
 *
 * Vendor audio HALs of 2.x (Galaxy S I9000 2.1/2.2: libaudio.so = AudioHardwareALSA with alsa-lib) talk to the
 * kernel's ALSA driver directly; without /dev/snd ALSAMixer dereferences a mixer that never opened and mediaserver
 * dies. This is a small sound card: one stereo playback device whose frames go to the same FIFO as the
 * emulator's AudioHardwareGeneric (<root>/dev/eac, 44.1 kHz s16 stereo, played by the app's AudioOut), one capture
 * device that gives silence, and a control device whose elements are made on first use by name (the asound.conf
 * hooks set "Playback Path", "Voice Call Path"... by name; the values are kept and read back).
 *
 * Layouts and numbers: uapi sound/asound.h (NDK r28c sysroot), as a 32-bit ARM EABI program sees them: long and
 * pointers 4 bytes, long long 8-byte aligned. Our qemu hands ioctls it does not know to the host unchanged, so
 * the argument is the guest's 32-bit struct (G() adds qemu's guest base). Protocol versions are those of Linux
 * 2.6.32 (the I9000 kernel): PCM 2.0.10, control 2.0.5.
 */

#define SND_RATE 44100
#define SND_FRAME 4                   /* s16 stereo */
/* status / control areas of the fd's file. qemu on a 16 KB-page host maps whole host pages, so it asks for the
 * offset rounded down to its page (0x80000000 - 0x1000 for a guest page at ...1000): each area has 64 KB around it */
#define SND_ST 0x10000
#define SND_CT 0x30000
#define SND_FILE 0x50000

enum { SND_CTL = 1, SND_PLAY = 2, SND_CAP = 3 };
struct snd_st {
    u64 ino, dev;
    int kind, state;
    u32 boundary, appl, avail_min, period, buffer;
    u8 *map;                          /* our view of the fd's file (status at SND_ST, control at SND_CT) */
};
static struct snd_st g_snd[8];
static int g_sndout = -1;             /* the FIFO the app plays */

#define SND_ELEMS 48
static struct { char name[44]; int val; } g_elem[SND_ELEMS];
static int g_nelem;

static struct snd_st *snd_of(int fd) {
    struct kstat st;
    if (fd < 0 || S2(NR_fstat, fd, &st) != 0) return 0;
    for (int i = 0; i < 8; i++) if (g_snd[i].kind && g_snd[i].ino == st.st_ino && g_snd[i].dev == st.st_dev) return &g_snd[i];
    return 0;
}

static long snd_open(const char *p, long flags) {
    int kind = seq(p, "/dev/snd/controlC0") ? SND_CTL : seq(p, "/dev/snd/pcmC0D0p") ? SND_PLAY :
               seq(p, "/dev/snd/pcmC0D0c") ? SND_CAP : 0;
    if (!kind) return -ENOENT;
    struct snd_st *s = 0;
    for (int i = 0; i < 8 && !s; i++) if (!g_snd[i].kind) s = &g_snd[i];
    if (!s) return -16;               /* EBUSY */
    long fd = S2(NR_memfd_create, "aemu-snd", (flags & 02000000) ? 1 : 0);
    if (fd < 0) return fd;
    S2(46 /*ftruncate*/, fd, SND_FILE);
    struct kstat st;
    S2(NR_fstat, fd, &st);
    memset(s, 0, sizeof *s);
    s->ino = st.st_ino; s->dev = st.st_dev; s->kind = kind;
    long m = S6(NR_mmap, 0, SND_FILE, 3, 1 /*MAP_SHARED*/, fd, 0);
    s->map = m < 0 && m > -4096 ? 0 : (u8 *)m;
    return fd;
}

/* the fd is closed: forget it once no descriptor of this process shows that file any more */
static void snd_closed(int fd) {
    struct snd_st *s = snd_of(fd);
    if (!s) return;
    S1(NR_close, fd);
    for (int f = 0; f < 1024; f++) {
        struct kstat st;
        if (S2(NR_fstat, f, &st) == 0 && st.st_ino == s->ino && st.st_dev == s->dev) return;
    }
    if (s->map) S2(215 /*munmap*/, s->map, SND_FILE);
    s->kind = 0;
}

/* mmap of the status (0x80000000) and control (0x81000000) pages: the matching area of the memfd */
static long snd_mmap_off(int fd, u64 off) {
    if (!snd_of(fd)) return -1;
    if (off >= 0x80000000UL - 0x10000 && off < 0x80000000UL + 0x10000) return (long)(off - 0x80000000UL) + SND_ST;
    if (off >= 0x81000000UL - 0x10000 && off < 0x81000000UL + 0x10000) return (long)(off - 0x81000000UL) + SND_CT;
    return -2;                        /* the sample buffer: not offered (access is RW_INTERLEAVED only) */
}

static void snd_sync(struct snd_st *s) {
    if (!s->map) return;
    u32 *st = (u32 *)(s->map + SND_ST), *ct = (u32 *)(s->map + SND_CT);
    st[0] = (u32)s->state; st[2] = s->appl;   /* hw_ptr = appl_ptr: what is written is played */
    ct[0] = s->appl; ct[1] = s->avail_min;
}

/* ---- control ---- */
static int elem_find(const u8 *id) {           /* by numid, else by name */
    u32 numid = *(const u32 *)id;
    if (numid) return numid <= (u32)g_nelem ? (int)numid - 1 : -1;
    const char *n = (const char *)id + 16;
    for (int i = 0; i < g_nelem; i++) if (seq(g_elem[i].name, n)) return i;
    if (!n[0] || g_nelem >= SND_ELEMS) return -1;
    int i = g_nelem++;
    u64 l = 0; while (l < 43 && n[l]) { g_elem[i].name[l] = n[l]; l++; } g_elem[i].name[l] = 0;
    g_elem[i].val = 0;
    return i;
}
static void elem_id(u8 *id, int i) {
    memset(id, 0, 64);
    *(u32 *)id = (u32)i + 1;
    *(u32 *)(id + 4) = 2;                      /* SNDRV_CTL_ELEM_IFACE_MIXER */
    memcpy(id + 16, g_elem[i].name, slen(g_elem[i].name));
}

static void pcm_info(u8 *p, int stream) {      /* struct snd_pcm_info, 288 bytes */
    memset(p, 0, 288);
    *(int *)(p + 8) = stream;
    memcpy(p + 16, "aemu", 4); memcpy(p + 80, "AEmulator PCM", 13); memcpy(p + 160, "subdevice #0", 12);
    *(u32 *)(p + 200) = 1; *(u32 *)(p + 204) = 1;
}

static long snd_ctl_ioctl(u32 cmd, u8 *p) {
    switch (cmd & 0xff) {
    case 0x00: *(int *)p = 0x020005; return 0;                       /* PVERSION */
    case 0x01:                                                       /* CARD_INFO, 376 bytes */
        memset(p, 0, 376);
        memcpy(p + 8, "aemu", 4); memcpy(p + 24, "aemu", 4); memcpy(p + 40, "AEmulator", 9);
        memcpy(p + 72, "AEmulator sound", 15); memcpy(p + 168, "AEmulator mixer", 15);
        return 0;
    case 0x10: {                                                     /* ELEM_LIST */
        u32 off = *(u32 *)p, space = *(u32 *)(p + 4), used = 0;
        u8 *ids = (u8 *)G(*(u32 *)(p + 16));
        for (u32 i = off; i < (u32)g_nelem && used < space; i++, used++) elem_id(ids + used * 64, (int)i);
        *(u32 *)(p + 8) = used; *(u32 *)(p + 12) = (u32)g_nelem;
        return 0;
    }
    case 0x11: {                                                     /* ELEM_INFO, 272 bytes */
        int i = elem_find(p);
        if (i < 0) return -ENOENT;
        elem_id(p, i);
        *(u32 *)(p + 64) = 2;                                        /* INTEGER */
        *(u32 *)(p + 68) = 3;                                        /* READWRITE */
        *(u32 *)(p + 72) = 1;                                        /* count */
        *(int *)(p + 76) = -1;
        *(int *)(p + 80) = 0; *(int *)(p + 84) = 255; *(int *)(p + 88) = 1;
        return 0;
    }
    case 0x12: case 0x13: {                                          /* ELEM_READ / ELEM_WRITE: value at 72 */
        int i = elem_find(p);
        if (i < 0) return -ENOENT;
        elem_id(p, i);
        if ((cmd & 0xff) == 0x12) *(int *)(p + 72) = g_elem[i].val;
        else { int c = g_elem[i].val != *(int *)(p + 72); g_elem[i].val = *(int *)(p + 72); return c; }
        return 0;
    }
    case 0x14: case 0x15: return 0;                                  /* ELEM_LOCK / UNLOCK */
    case 0x16: if (*(int *)p < 0) *(int *)p = 0; return 0;           /* SUBSCRIBE_EVENTS */
    case 0x30: *(int *)p = *(int *)p < 0 ? 0 : -1; return 0;         /* PCM_NEXT_DEVICE */
    case 0x31: {                                                     /* PCM_INFO */
        u32 dev = *(u32 *)p, sub = *(u32 *)(p + 4); int stream = *(int *)(p + 8);
        if (dev != 0 || sub != 0) return -ENOENT;
        pcm_info(p, stream);
        return 0;
    }
    case 0x32: return 0;                                             /* PCM_PREFER_SUBDEVICE */
    case 0x20: case 0x40: *(int *)p = -1; return 0;                  /* HWDEP / RAWMIDI_NEXT_DEVICE: none */
    case 0xd1: *(int *)p = 0; return 0;                              /* POWER_STATE: D0 */
    case 0xd0: return 0;
    }
    return -25;                                                      /* ENOTTY */
}

/* ---- hw params: fixed 44.1 kHz s16 stereo, interleaved read/write ---- */
struct ivl { u32 min, max, fl; };                                    /* fl: openmin 1, openmax 2, integer 4, empty 8 */
static int ivl_set(struct ivl *v, u32 lo, u32 hi) {                 /* intersect with [lo, hi] */
    if (v->min < lo || (v->min == lo && (v->fl & 1) == 0)) { if (v->min < lo) { v->min = lo; v->fl &= ~1u; } }
    if (v->max > hi) { v->max = hi; v->fl &= ~2u; }
    if (v->min > v->max || (v->min == v->max && (v->fl & 3))) { v->fl |= 8; return -1; }
    return 0;
}
static u32 udiv_up(u64 a, u64 b) { return (u32)((a + b - 1) / b); }

static long snd_hw(struct snd_st *s, u8 *p, int commit) {
    u32 *masks = (u32 *)(p + 4);                                     /* 3 masks of 8 words */
    struct ivl *iv = (struct ivl *)(p + 260);                        /* params 8..19 */
    masks[0] &= 1u << 3;                                             /* ACCESS: RW_INTERLEAVED */
    masks[8] &= 1u << 2;                                             /* FORMAT: S16_LE */
    masks[16] &= 1u;                                                 /* SUBFORMAT: STD */
    for (int w = 1; w < 8; w++) masks[w] = masks[8 + w] = masks[16 + w] = 0;
    if (!masks[0] || !masks[8] || !masks[16]) return -22;
    struct ivl *sb = &iv[0], *fb = &iv[1], *ch = &iv[2], *rt = &iv[3], *pt = &iv[4], *ps = &iv[5], *pb = &iv[6],
               *np = &iv[7], *bt = &iv[8], *bs = &iv[9], *bb = &iv[10], *tt = &iv[11];
    if (ivl_set(sb, 16, 16) || ivl_set(fb, 32, 32) || ivl_set(ch, 2, 2) || ivl_set(rt, SND_RATE, SND_RATE)) return -22;
    if (ivl_set(ps, 32, 16384) || ivl_set(bs, 64, 65536) || ivl_set(np, 2, 1024)) return -22;
    for (int k = 0; k < 4; k++) {
        /* bytes and times bound the sizes ... */
        if (ivl_set(ps, udiv_up(pb->min, SND_FRAME), pb->max / SND_FRAME)) return -22;
        if (ivl_set(bs, udiv_up(bb->min, SND_FRAME), bb->max / SND_FRAME)) return -22;
        if (ivl_set(ps, udiv_up((u64)pt->min * SND_RATE, 1000000), (u32)((u64)pt->max * SND_RATE / 1000000))) return -22;
        if (ivl_set(bs, udiv_up((u64)bt->min * SND_RATE, 1000000), (u32)((u64)bt->max * SND_RATE / 1000000))) return -22;
        /* ... buffer = period * periods */
        if (ivl_set(bs, ps->min * np->min, (u32)((u64)ps->max * np->max > 65536 ? 65536 : ps->max * np->max))) return -22;
        if (ivl_set(np, udiv_up(bs->min, ps->max), bs->max / ps->min)) return -22;
        if (ivl_set(ps, udiv_up(bs->min, np->max), bs->max / np->min)) return -22;
        /* and the sizes give back bytes and times */
        pb->min = ps->min * SND_FRAME; pb->max = ps->max * SND_FRAME; pb->fl = 4;
        bb->min = bs->min * SND_FRAME; bb->max = bs->max * SND_FRAME; bb->fl = 4;
        pt->min = (u32)((u64)ps->min * 1000000 / SND_RATE); pt->max = udiv_up((u64)ps->max * 1000000, SND_RATE);
        pt->fl = ((u64)pt->min * SND_RATE != (u64)ps->min * 1000000 ? 1 : 0) | ((u64)pt->max * SND_RATE != (u64)ps->max * 1000000 ? 2 : 0);
        bt->min = (u32)((u64)bs->min * 1000000 / SND_RATE); bt->max = udiv_up((u64)bs->max * 1000000, SND_RATE);
        bt->fl = ((u64)bt->min * SND_RATE != (u64)bs->min * 1000000 ? 1 : 0) | ((u64)bt->max * SND_RATE != (u64)bs->max * 1000000 ? 2 : 0);
    }
    ps->fl |= 4; bs->fl |= 4; np->fl |= 4;
    if (tt->min > tt->max) return -22;
    *(u32 *)(p + 512) = 0;                                           /* rmask */
    *(u32 *)(p + 516) = ~0u;                                         /* cmask */
    *(u32 *)(p + 520) = 0x100 | 0x10000;                             /* info: INTERLEAVED | BLOCK_TRANSFER */
    *(u32 *)(p + 524) = 16;                                          /* msbits */
    *(u32 *)(p + 528) = SND_RATE; *(u32 *)(p + 532) = 1;
    *(u32 *)(p + 536) = 0;
    if (commit) {
        if (ps->min != ps->max || bs->min != bs->max) return -22;
        s->period = ps->min; s->buffer = bs->min; s->state = 1;      /* SETUP */
        snd_sync(s);
    }
    return 0;
}

static void snd_status(struct snd_st *s, u8 *p, u32 size) {        /* struct snd_pcm_status (32-bit) */
    memset(p, 0, size);
    struct { long s, ns; } now;
    S2(113, 1, &now);
    *(int *)p = s->state;
    *(int *)(p + 12) = (int)now.s; *(int *)(p + 16) = (int)now.ns;
    *(u32 *)(p + 20) = s->appl; *(u32 *)(p + 24) = s->appl;
    *(u32 *)(p + 28) = 0;                                            /* delay */
    *(u32 *)(p + 32) = s->kind == SND_PLAY ? s->buffer : 0;         /* avail */
    *(u32 *)(p + 36) = s->buffer;
}

static void snd_pace(u32 frames) {
    struct { long s, ns; } t = { 0, (long)((u64)frames * 1000000000UL / SND_RATE) };
    while (t.ns >= 1000000000) { t.s++; t.ns -= 1000000000; }
    S2(101 /*nanosleep*/, &t, 0);
}

static long snd_write(struct snd_st *s, const u8 *buf, u32 frames) {
    if (s->state == 1 || s->state == 0) return -77;                  /* EBADFD: not prepared */
    if (g_sndout < 0) {
        char path[600];
        memcpy(path, g_root, g_rootlen); memcpy(path + g_rootlen, "/dev/eac", 9);
        g_sndout = (int)S4(NR_openat, AT_FDCWD, path, 01 | 04000 /*O_WRONLY|O_NONBLOCK*/ | 02000000, 0);
        if (g_sndout >= 0) S3(25 /*fcntl*/, g_sndout, 4 /*F_SETFL*/, 01);   /* blocking: the player paces us */
    }
    long n = (long)frames * SND_FRAME, done = 0;
    while (g_sndout >= 0 && done < n) {
        long w = S3(NR_write, g_sndout, buf + done, n - done);
        if (w == -4) continue;
        if (w <= 0) { S1(NR_close, g_sndout); g_sndout = -1; break; }
        done += w;
    }
    if (g_sndout < 0) snd_pace(frames);                              /* nobody listens: keep real time anyway */
    s->state = 3;                                                    /* RUNNING */
    s->appl += frames; if (s->boundary && s->appl >= s->boundary) s->appl -= s->boundary;
    snd_sync(s);
    return 0;
}

static long snd_pcm_ioctl(struct snd_st *s, u32 cmd, u8 *p) {
    switch (cmd & 0xff) {
    case 0x00: *(int *)p = 0x02000a; return 0;                       /* PVERSION 2.0.10 */
    case 0x01: pcm_info(p, s->kind == SND_CAP); return 0;            /* INFO */
    case 0x02: case 0x03: case 0x04: return 0;                       /* TSTAMP, TTSTAMP, USER_PVERSION */
    case 0x10: return snd_hw(s, p, 0);                               /* HW_REFINE */
    case 0x11: return snd_hw(s, p, 1);                               /* HW_PARAMS */
    case 0x12: s->state = 0; return 0;                               /* HW_FREE */
    case 0x13:                                                       /* SW_PARAMS */
        s->avail_min = *(u32 *)(p + 12); s->boundary = *(u32 *)(p + 36);
        snd_sync(s);
        return 0;
    case 0x20: case 0x24: snd_status(s, p, (cmd >> 16) & 0x3fff); return 0;   /* STATUS(_EXT) */
    case 0x21: *(int *)p = 0; return 0;                              /* DELAY */
    case 0x22: return 0;                                             /* HWSYNC */
    case 0x23: {                                                     /* SYNC_PTR (32-bit) */
        u32 fl = *(u32 *)p;
        if (fl & 2) {} else s->appl = *(u32 *)(p + 68);              /* !APPL: the caller's appl_ptr is new */
        if (!(fl & 4)) s->avail_min = *(u32 *)(p + 72);
        memset(p + 4, 0, 64);
        *(int *)(p + 4) = s->state; *(u32 *)(p + 12) = s->appl;
        *(u32 *)(p + 68) = s->appl; *(u32 *)(p + 72) = s->avail_min;
        snd_sync(s);
        return 0;
    }
    case 0x40: case 0x41: s->state = 2; s->appl = 0; snd_sync(s); return 0;  /* PREPARE, RESET */
    case 0x42: s->state = 3; snd_sync(s); return 0;                  /* START */
    case 0x43: case 0x44: s->state = 1; snd_sync(s); return 0;       /* DROP, DRAIN */
    case 0x45: s->state = *(int *)p ? 6 : 3; snd_sync(s); return 0;  /* PAUSE */
    case 0x46: case 0x49: *(u32 *)p = 0; return 0;                   /* REWIND / FORWARD: nothing to move */
    case 0x47: return 0;                                             /* RESUME */
    case 0x48: s->state = 4; snd_sync(s); return 0;                  /* XRUN */
    case 0x50: {                                                     /* WRITEI_FRAMES */
        if (s->kind != SND_PLAY) return -22;
        u32 frames = *(u32 *)(p + 8);
        long r = snd_write(s, (const u8 *)G(*(u32 *)(p + 4)), frames);
        if (r < 0) return r;
        *(int *)p = (int)frames;
        return 0;
    }
    case 0x51: {                                                     /* READI_FRAMES: silence in real time */
        if (s->kind != SND_CAP) return -22;
        u32 frames = *(u32 *)(p + 8);
        memset((void *)G(*(u32 *)(p + 4)), 0, (u64)frames * SND_FRAME);
        snd_pace(frames);
        s->state = 3; s->appl += frames; if (s->boundary && s->appl >= s->boundary) s->appl -= s->boundary;
        snd_sync(s);
        *(int *)p = (int)frames;
        return 0;
    }
    case 0x60: case 0x61: return 0;                                  /* LINK / UNLINK */
    }
    return -25;
}

static long snd_ioctl(int fd, u32 cmd, u64 arg) {
    struct snd_st *s = snd_of(fd);
    if (!s) return -25;
    u8 *p = (u8 *)G(arg);
    return s->kind == SND_CTL ? snd_ctl_ioctl(cmd, p) : snd_pcm_ioctl(s, cmd, p);
}
