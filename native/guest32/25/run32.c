// aemu32 - runs the firmware's own 32-bit ARM programs natively on a phone whose cores still execute AArch32
// (Cortex-A76/A78/A55 class: Helio G99, Dimensity 920, Snapdragon up to 8 Gen 1). A copy of the arm64 kernel of
// the same Android release (native/guest64/25) for the ARM EABI: the 64-bit kernel execs this one instead of
// qemu-arm when the host can run 32-bit code (AEMU64_ARM32), and the firmware's 32-bit programs then run at full
// speed. Its own seccomp filter (AUDIT_ARCH_ARM) traps the calls that must see the guest's world.
//
//   aemu32 <argv0> <guest program> [args...]       AEMU64_ROOT=<image root>
//
// Not tested on a phone yet (20.10: the test phone, SM8750, has no AArch32).

typedef unsigned long long u64;     // binder's protocol words and file sizes are 64-bit on the 32-bit ABI too
typedef long long i64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long ul;           // a machine word: pointers, sizes in kernel structures, syscall arguments

#define IMAGE_BASE 0x7e000000u      /* see build32.sh: calls made from this image pass the filter */
#define IMAGE_SPAN 0x00100000u

long sc(long nr, long a, long b, long c, long d, long e, long f);
void enter_guest(void *entry, void *sp) __attribute__((noreturn));
#define S0(n) sc(n, 0, 0, 0, 0, 0, 0)
#define S1(n, a) sc(n, (long)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) sc(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) sc(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define S4(n, a, b, c, d) sc(n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define S5(n, a, b, c, d, e) sc(n, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0)
#define S6(n, a, b, c, d, e, f) sc(n, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))

// ARM EABI numbers. The names are those of the arm64 kernel, so the trap code reads the same: NR_newfstatat is
// fstatat64, NR_fstat fstat64, NR_mmap mmap2 (offset in pages), NR_statfs statfs64 (path, size, buf), the id calls
// their *32 forms.
enum {
    NR_setxattr = 226, NR_lsetxattr = 227, NR_fsetxattr = 228, NR_getxattr = 229, NR_lgetxattr = 230, NR_fgetxattr = 231,
    NR_listxattr = 232, NR_llistxattr = 233, NR_removexattr = 235, NR_lremovexattr = 236, NR_getcwd = 183, NR_dup = 41,
    NR_dup3 = 358, NR_fcntl = 221, NR_inotify_add_watch = 317, NR_ioctl = 54,
    NR_mknodat = 324, NR_mkdirat = 323, NR_unlinkat = 328, NR_symlinkat = 331, NR_linkat = 330, NR_renameat = 329,
    NR_umount2 = 52, NR_mount = 21, NR_statfs = 266, NR_truncate = 193, NR_ftruncate = 194, NR_faccessat = 334,
    NR_chdir = 12, NR_chroot = 61, NR_fchmodat = 333, NR_fchownat = 325, NR_fchown = 207, NR_openat = 322, NR_close = 6,
    NR_lseek = 19, NR_read = 3, NR_write = 4, NR_pread64 = 180, NR_pwrite64 = 181, NR_readlinkat = 332, NR_newfstatat = 327, NR_fstat = 197,
    NR_timerfd_create = 350, NR_utimensat = 348, NR_capget = 184, NR_capset = 185, NR_exit_group = 248, NR_unshare = 337,
    NR_sched_yield = 158, NR_rt_sigaction = 174, NR_rt_sigprocmask = 175, NR_reboot = 88,
    NR_setregid = 204, NR_setgid = 214, NR_setreuid = 203, NR_setuid = 213, NR_setresuid = 208, NR_getresuid = 209,
    NR_setresgid = 210, NR_getresgid = 211, NR_setfsuid = 215, NR_setfsgid = 216, NR_getgroups = 205, NR_setgroups = 206,
    NR_prctl = 172, NR_getpid = 20, NR_getuid = 199, NR_geteuid = 201, NR_getgid = 200, NR_getegid = 202, NR_gettid = 224,
    NR_socket = 281, NR_socketpair = 288, NR_bind = 282, NR_connect = 283, NR_getsockname = 286, NR_sendto = 290, NR_setsockopt = 294, NR_getsockopt = 295,
    NR_sendmsg = 296, NR_recvmsg = 297, NR_munmap = 91, NR_execve = 11, NR_mmap = 192, NR_mprotect = 125,
    NR_renameat2 = 382, NR_memfd_create = 385, NR_statx = 397,
    // the old path calls a 32-bit libc may still make (bionic of 5.0+ uses the *at forms; these are trapped too)
    NR_open = 5, NR_access = 33, NR_stat64 = 195, NR_lstat64 = 196, NR_readlink = 85, NR_mkdir = 39, NR_rmdir = 40,
    NR_unlink = 10, NR_rename = 38, NR_chmod = 15, NR_chown32 = 212, NR_lchown32 = 198, NR_symlink = 83, NR_link = 9,
};

#define AT_FDCWD (-100)
#define ENOENT 2
#define ENOEXEC 8
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define ENAMETOOLONG 36
#define ENOSYS 38
#define PATH_MAX 4096
#define SIGSYS 31
#define SYSBIT (1UL << (SIGSYS - 1))

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static int pre(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
void *memcpy(void *d, const void *s, u64 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, u64 n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }
void *memmove(void *d, const void *s, u64 n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) while (n--) *a++ = *b++; else while (n--) a[n] = b[n];
    return d;
}
static int g_sayfd = -1;          // crash reports also go to <root>/dev/aemu_crash.log
static void say(const char *s) { S3(NR_write, 2, s, slen(s)); if (g_sayfd >= 0) S3(NR_write, g_sayfd, s, slen(s)); }
static void hex(ul v) { char b[11] = "0x"; for (int i = 0; i < 8; i++) b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[10] = 0; say(b); }
static void where(ul addr);
static void die(const char *a, const char *b) { say("aemu32: "); say(a); say(b); say("\n"); S1(NR_exit_group, 127); for (;;); }
static u64 atou(const char *s) { u64 v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (u64)(*s++ - '0'); return v; }
static char *utoa(u64 v, char *end) { *--end = 0; do *--end = (char)('0' + v % 10); while (v /= 10); return end; }

// ---------------------------------------------------------------------------------------------------- state

static char g_root[512];        // image root on the host, no trailing slash
static u64 g_rootlen;
static char g_self[512];        // this program
static char g_props[512];       // host file behind /dev/__properties__
static char g_exe[PATH_MAX];    // guest view of the running program
static int g_uid, g_gid;        // the ids the guest believes it has
static u32 g_real_uid;
static char g_k64[512];          // the arm64 kernel of this release, for a 64-bit program this one execs (AEMU64_KERNEL64)
static char g_qemu[512];        // qemu-arm for the firmware's 32-bit programs (AEMU64_QEMU)
static int g_crash;
static u64 g_logdev, g_logino;      // the service log on descriptor 2 (a file or a pipe to the app)
static int g_logpipe;
static u64 g_inino;                 // ...and the pipe on descriptor 0
static char g_sd[512]; static u64 g_sdlen;   // the shared card folder on the phone (AEMU64_SDCARD)
static const int g_is32 = 0;      // (the arm64 kernel: "this process is qemu-arm"; never so here)
static void (*g_fatal[32])(int, void *, void *);
static void fatal_relay(int sig, void *info, void *uc);
// qemu-arm's signal handler blocks every signal in the context it returns to (until the guest's handler is
// entered); with SIGSYS blocked the next trapped call would kill the process, so that bit is taken back out.

// The image has the engine's 32-bit SwiftShader (system/lib/egl): 32-bit programs then have GLES 2 like the 64-bit
// ones (WebView, a vendor browser on its own Chromium), and nothing is hidden from them.
static int gles2_for_32(void) {
    static int known = -1;
    if (known < 0) {
        static char p[600];
        memcpy(p, g_root, g_rootlen); memcpy(p + g_rootlen, "/system/lib/egl/libGLESv2_swiftshader.so", 41);
        known = S4(NR_faccessat, AT_FDCWD, p, 0, 0) == 0;
    }
    return known;
}
static int is_fatal(long sig) { return sig == 11 || sig == 7 || sig == 4 || sig == 6 || sig == 8; }            // AEMU64_CRASH: report fatal signals ourselves

// ---------------------------------------------------------------------------------------------------- paths

// Lexically normalises an absolute guest path into out (no "//", ".", ".."), so it cannot climb out of the root.
static int norm(const char *p, char *out, u64 cap) {
    u64 o = 0;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = p; while (*e && *e != '/') e++;
        u64 n = (u64)(e - p);
        if (n == 1 && p[0] == '.') { p = e; continue; }
        if (n == 2 && p[0] == '.' && p[1] == '.') { while (o && out[--o] != '/'); p = e; continue; }
        if (o + n + 2 > cap) return -ENAMETOOLONG;
        out[o++] = '/'; memcpy(out + o, p, n); o += n; p = e;
    }
    if (!o) out[o++] = '/';
    out[o] = 0;
    return 0;
}

static int host_dev(const char *n) {
    static const char *const pass[] = { "null", "zero", "full", "random", "urandom", "tty", "ptmx", 0 };
    for (int i = 0; pass[i]; i++) if (seq(n, pass[i])) return 1;
    return pre(n, "pts/") || seq(n, "pts");
}

// Guest path → host path. Relative paths stay as they are (the working directory is already inside the root).
// Returns the path to use, or 0 with *err set.
static const char *xl(const char *p, char *buf, int *err) {
    if (!p) return p;
    if (p[0] != '/') return p;
    char *n = buf + g_rootlen;              // normalised straight into its place after the root
    if ((*err = norm(p, n, PATH_MAX - g_rootlen))) return 0;
    if ((pre(n, "/proc") && (n[5] == '/' || !n[5])) || (pre(n, "/sys") && (n[4] == '/' || !n[4]))) {
        // the image's own files win (fake sysfs, /proc/mounts); the rest is the phone's. No SELinux for the guest,
        // and none of the phone's suspend controls (reading its wakeup_count blocks the guest's power manager).
        // under qemu-arm the process has the translator's own helper thread; zygote waits before every fork until
        // /proc/self/task lists one thread, so 32-bit programs see a list of one
        if (g_is32 && n[5] == '/') {
            const char *q = n + 6; u64 pid = 0;
            if (pre(q, "self")) q += 4; else { while (*q >= '0' && *q <= '9') pid = pid * 10 + (u64)(*q++ - '0'); if (pid != (u64)S0(172 /* getpid */)) q = ""; }
            if (seq(q, "/task")) memcpy(n, "/proc/.one", 11);
        }
        memcpy(buf, g_root, g_rootlen);
        if (pre(n, "/sys/fs/selinux") || pre(n, "/sys/power") || S4(NR_faccessat, AT_FDCWD, buf, 0, 0) == 0) return buf;
        return p;
    }
    // translated code is slow: the 32-bit zygote starts without its minutes of class preloading
    if (g_is32 && seq(n, "/system/etc/preloaded-classes")) memcpy(n, "/proc/.task1/none", 18);
    if (pre(n, "/dev/") && host_dev(n + 5)) return p;
    if (g_props[0] && seq(n, "/dev/__properties__")) return g_props;
    if (pre(n, g_root) && (n[g_rootlen] == '/' || !n[g_rootlen])) return p;   // already a host path (our own argv)
    if (g_sdlen) {
        // the guest's card: plain directories for it, the shared folder of the phone underneath
        static const char *const sd[] = { "/storage/emulated/0", "/storage/emulated/legacy", "/storage/self/primary", "/sdcard",
                                          "/mnt/sdcard", "/mnt/user/0/primary", "/mnt/shell/emulated/0", "/storage/sdcard0", 0 };
        for (int i = 0; sd[i]; i++) {
            u64 l = slen(sd[i]);
            if (!pre(n, sd[i]) || (n[l] != '/' && n[l])) continue;
            u64 rest = slen(n + l);
            if (g_sdlen + rest + 1 > 3 * PATH_MAX - PATH_MAX) break;
            memmove(buf + g_sdlen, n + l, rest + 1);
            memcpy(buf, g_sd, g_sdlen);
            return buf;
        }
    }
    memcpy(buf, g_root, g_rootlen);
    if (!n[1]) n[0] = 0;
    return buf;
}

// host path → guest path, in place
static void unxl(char *p) {
    if (g_sdlen && pre(p, g_sd) && (p[g_sdlen] == '/' || !p[g_sdlen])) {
        static const char g[] = "/storage/emulated/0";
        u64 rest = slen(p + g_sdlen);
        memmove(p + sizeof g - 1, p + g_sdlen, rest + 1);
        memcpy(p, g, sizeof g - 1);
        return;
    }
    if (!pre(p, g_root)) return;
    if (!p[g_rootlen]) { p[0] = '/'; p[1] = 0; return; }
    if (p[g_rootlen] == '/') memmove(p, p + g_rootlen, slen(p + g_rootlen) + 1);
}

// ---------------------------------------------------------------------------------------------------- stat

struct kstat {                      // ARM struct stat64 (fstatat64, fstat64)
    u64 st_dev; u32 __pad0, __st_ino, st_mode, st_nlink, st_uid, st_gid; u64 st_rdev; u32 __pad3; i64 st_size;
    u32 st_blksize; u64 st_blocks; u32 at, atn, mt, mtn, ct, ctn; u64 st_ino;
};

// files of the image belong to the app's uid; the guest expects its own system to belong to root
static void fake_owner(struct kstat *st) {
    if (st->st_uid == g_real_uid) st->st_uid = 0;
    if (st->st_gid == g_real_uid) st->st_gid = 0;
    if ((st->st_mode & 0170000) != 0120000 && !(st->st_mode & 0077) && (st->st_mode & 0170000) != 0020000) {
        // the importer keeps everything 0700/0600: show the usual world-readable modes
        st->st_mode |= ((st->st_mode & 0500) >> 3) | ((st->st_mode & 0500) >> 6);
    }
}

// ---------------------------------------------------------------------------------------------------- ashmem

#define ASHMEM_MAGIC 0x77
// an ashmem region is a memfd of ours; it may have come from another process, so it is recognised by its name
static int is_ashmem_fd(int fd) {
    char p[40], t[32], d[24];
    memcpy(p, "/proc/self/fd/", 14);
    char *n = utoa((u64)fd, d + sizeof d); memcpy(p + 14, n, slen(n) + 1);
    long r = S4(NR_readlinkat, AT_FDCWD, p, t, sizeof t - 1);
    if (r < 18) return 0;
    t[r] = 0;
    return pre(t, "/memfd:aemu-ashmem");
}

static long ashmem_ioctl(int fd, u32 cmd, u64 arg) {
    switch (cmd & 0xff) {
    case 1: return 0;                                               // SET_NAME
    case 2: if (arg) *(char *)arg = 0; return 0;                    // GET_NAME
    case 3: return sc(NR_ftruncate, fd, 0, (long)arg, 0, 0, 0);                       // SET_SIZE
    case 4: { struct kstat st; long r = S2(NR_fstat, fd, &st); return r < 0 ? r : st.st_size; } // GET_SIZE
    case 5: return 0;                                               // SET_PROT_MASK
    case 6: return 7;                                               // GET_PROT_MASK
    case 7: return 0;                                               // PIN
    case 8: return 0;                                               // UNPIN
    case 9: return 0;                                               // GET_PIN_STATUS
    case 10: return 0;                                              // PURGE
    default: return 0;
    }
}

static long fake_memfile(const char *name, const char *content, u64 len) {
    long fd = S2(NR_memfd_create, name, 1 /*MFD_CLOEXEC*/);
    if (fd < 0) return fd;
    S3(NR_write, fd, content, len);
    S3(NR_lseek, fd, 0, 0 /*SEEK_SET*/);
    return fd;
}

// ---------------------------------------------------------------------------------------------------- netlink emulation
struct aemu_nlmsghdr {
    u32 len;
    u16 type;
    u16 flags;
    u32 seq;
    u32 pid;
};
struct aemu_nlmsgerr {
    int error;
    struct aemu_nlmsghdr msg;
};

#define MAX_NL 32
static struct { int fd, peer; } g_nl[MAX_NL];

static void nl_register(int fd, int peer) {
    for (int i = 0; i < MAX_NL; i++) {
        if (!g_nl[i].fd) {
            g_nl[i].fd = fd;
            g_nl[i].peer = peer;
            return;
        }
    }
}

static int nl_find_peer(int fd) {
    for (int i = 0; i < MAX_NL; i++) {
        if (g_nl[i].fd == fd) return g_nl[i].peer;
    }
    return -1;
}

static int is_nl_fd(int fd) {
    for (int i = 0; i < MAX_NL; i++) {
        if (g_nl[i].fd == fd) return 1;
    }
    return 0;
}

static void nl_close(int fd) {
    for (int i = 0; i < MAX_NL; i++) {
        if (g_nl[i].fd == fd) {
            if (g_nl[i].peer >= 0) S1(NR_close, g_nl[i].peer);
            g_nl[i].fd = 0;
            g_nl[i].peer = -1;
        }
    }
}

// Every request gets the answer that lets its sender go on: a dump ends at once (nothing to list), a request that
// asked for an acknowledgement gets it, and one that only waits for data (family lookups) is told "not supported".
static void nl_handle_send(int fd, const void *buf, u64 len) {
    int peer = nl_find_peer(fd);
    if (!buf || peer < 0) return;
    const char *p = buf, *end = p + len;
    while (p + sizeof(struct aemu_nlmsghdr) <= end) {
        const struct aemu_nlmsghdr *req = (const struct aemu_nlmsghdr *)p;
        if (req->len < sizeof *req || p + req->len > end) break;
        struct { struct aemu_nlmsghdr hdr; struct aemu_nlmsgerr err; } m;
        m.hdr.seq = req->seq; m.hdr.pid = req->pid;
        if ((req->flags & 0x300) == 0x300) {        // NLM_F_DUMP
            m.hdr.len = sizeof m.hdr + 4; m.hdr.type = 3 /*NLMSG_DONE*/; m.hdr.flags = 2 /*NLM_F_MULTI*/;
            m.err.error = 0;
        } else {
            m.hdr.len = sizeof m; m.hdr.type = 2 /*NLMSG_ERROR*/; m.hdr.flags = 0;
            m.err.error = (req->flags & 4 /*NLM_F_ACK*/) ? 0 : -95 /*EOPNOTSUPP*/;
            m.err.msg = *req;
        }
        S3(NR_write, peer, &m, m.hdr.len);
        p += (req->len + 3) & ~3U;
    }
}

struct sockaddr_un { u16 family; char path[108]; };
#include "bclient.c"

// ---------------------------------------------------------------------------------------------------- framebuffer

// /dev/graphics/fb0 is a plain file of two RGBX pages (the app's screen view maps it); the guest's gralloc asks
// the usual fbdev questions, answered here from <root>/dhd.fbgeom ("width height").
static u32 g_fbw, g_fbh;

static void fb_geom(void) {
    char p[600], t[64];
    memcpy(p, g_root, g_rootlen); memcpy(p + g_rootlen, "/dhd.fbgeom", 12);
    long fd = S4(NR_openat, AT_FDCWD, p, 0, 0);
    g_fbw = 720; g_fbh = 1280;
    if (fd < 0) return;
    long n = S3(NR_read, fd, t, sizeof t - 1);
    S1(NR_close, fd);
    if (n <= 0) return;
    t[n] = 0;
    const char *q = t; u32 w = (u32)atou(q);
    while (*q >= '0' && *q <= '9') q++;
    while (*q == ' ') q++;
    u32 h = (u32)atou(q);
    if (w && h) { g_fbw = w; g_fbh = h; }
}

static long fb_ioctl(u32 cmd, u64 arg) {
    if (!g_fbw) fb_geom();
    switch (cmd) {
    case 0x4600: {                              // FBIOGET_VSCREENINFO
        u32 *v = (u32 *)arg;
        memset(v, 0, 160);
        v[0] = g_fbw; v[1] = g_fbh; v[2] = g_fbw; v[3] = g_fbh; v[6] = 32;   // one page: gralloc copies each frame in on post
        v[8] = 0; v[9] = 8; v[11] = 8; v[12] = 8; v[14] = 16; v[15] = 8; v[17] = 24; v[18] = 8;   // R, G, B, A
        return 0;
    }
    case 0x4602: {                              // FBIOGET_FSCREENINFO
        u8 *f = (u8 *)arg;
        memset(f, 0, 80);
        memcpy(f, "aemu", 5);
        *(u32 *)(f + 24) = g_fbw * g_fbh * 4;       // smem_len
        *(u32 *)(f + 36) = 2;                       // FB_VISUAL_TRUECOLOR
        *(u16 *)(f + 42) = 1;                       // ypanstep
        *(u32 *)(f + 48) = g_fbw * 4;               // line_length
        return 0;
    }
    case 0x4601: case 0x4606: case 0x4611: return 0;    // PUT_VSCREENINFO, PAN_DISPLAY, BLANK
    }
    return -25; /* ENOTTY */
}

// ---------------------------------------------------------------------------------------------------- input

// /dev/input/event0: a stream of struct input_event from the app (AEMU64_INPUT socket); the evdev questions of the
// guest's EventHub are answered here: a direct multi-touch screen with the navigation and volume keys.
static char g_input[108];
static int g_infds[4];                          // descriptors + 1

static int is_input_fd(int fd) { for (int i = 0; i < 4; i++) if (g_infds[i] == fd + 1) return 1; return 0; }

static long input_open(long flags) {
    if (!g_input[0]) return -ENOENT;
    long s = S3(NR_socket, 1, 1 | (flags & 02004000), 0);
    if (s < 0) return s;
    struct sockaddr_un sa; sa.family = 1;
    u64 l = slen(g_input); memcpy(sa.path, g_input, l + 1);
    long r = S3(NR_connect, s, &sa, 2 + l + 1);
    if (r < 0) { S1(NR_close, s); return r; }
    for (int i = 0; i < 4; i++) if (!g_infds[i] || g_infds[i] == (int)s + 1) { g_infds[i] = (int)s + 1; break; }
    return s;
}

static void setbit(u8 *m, u64 len, u32 bit) { if (bit / 8 < len) m[bit / 8] |= (u8)(1 << (bit % 8)); }

static long input_ioctl(u32 cmd, u64 arg) {
    u32 nr = cmd & 0xff, len = (cmd >> 16) & 0x3fff;
    u8 *out = (u8 *)arg;
    if (!g_fbw) fb_geom();
    if (nr == 0x01) { *(int *)arg = 0x010001; return 0; }                        // EVIOCGVERSION
    if (nr == 0x02) { u16 *id = (u16 *)arg; id[0] = 0x18; id[1] = 1; id[2] = 1; id[3] = 1; return 0; }   // EVIOCGID
    if (nr == 0x06) { static const char name[] = "dhd-touch"; u64 n = sizeof name < len ? sizeof name : len; memcpy(out, name, n); return (long)n; }
    if (nr == 0x07 || nr == 0x08) return -ENOENT;                                // phys, uniq
    if (nr == 0x09) { memset(out, 0, len); setbit(out, len, 1); return len; }    // INPUT_PROP_DIRECT
    if (nr == 0x18 || nr == 0x19 || nr == 0x1b) { memset(out, 0, len); return len; }   // key, led, switch state
    if (nr >= 0x20 && nr < 0x40) {                                               // EVIOCGBIT(type)
        memset(out, 0, len);
        switch (nr - 0x20) {
        case 0: setbit(out, len, 0); setbit(out, len, 1); setbit(out, len, 3); break;
        case 1: for (u32 k = 1; k < 0xf0; k++) setbit(out, len, k); setbit(out, len, 580); break;
        case 3: setbit(out, len, 48); setbit(out, len, 50); setbit(out, len, 53); setbit(out, len, 54); setbit(out, len, 57); break;
        }
        return len;
    }
    if (nr >= 0x40 && nr < 0x80) {                                               // EVIOCGABS(axis)
        int *a = (int *)arg; u32 ax = nr - 0x40;
        memset(a, 0, 24);
        if (ax == 53) a[2] = (int)g_fbw - 1; else if (ax == 54) a[2] = (int)g_fbh - 1;
        else if (ax == 48 || ax == 50) a[2] = 255; else if (ax == 57) a[2] = 31; else return -EINVAL;
        return 0;
    }
    if (nr == 0x90 || nr == 0xa0 || nr == 0x03) return 0;                        // grab, clock id, repeat
    return -EINVAL;
}

// ---------------------------------------------------------------------------------------------------- exec

static int env_is_ours(const char *e) { return pre(e, "AEMU64_"); }

static long do_exec(const char *path, char *const argv[], char *const envp[]) {
    static char buf[PATH_MAX]; int err = 0;      /* exec replaces the process: nobody else is left to share it */
    const char *host = xl(path, buf, &err);
    if (!host) return err;
    long r = S4(NR_faccessat, AT_FDCWD, host, 1 /*X_OK*/, 0);
    if (r < 0) return r;
    u64 na = 0, ne = 0;
    if (argv) while (argv[na]) na++;
    if (envp) while (envp[ne]) ne++;
    ul need = (na + ne + 28) * sizeof(char *);
    char **v = (char **)S6(NR_mmap, 0, need, 3, 0x22, -1, 0);
    if ((ul)v >= (ul)-4095) return -ENOMEM;
    char **a = v, **e;
    *a++ = g_self;
    *a++ = (char *)(na ? argv[0] : path);
    *a++ = (char *)host;
    for (u64 i = 1; i < na; i++) *a++ = argv[i];
    *a++ = 0;
    e = a;
    for (u64 i = 0; i < ne; i++) if (!env_is_ours(envp[i])) *a++ = envp[i];
    static char e_root[540], e_props[540], e_binder[140], e_hwbinder[140], e_vndbinder[140], e_id[64], e_f[] = "AEMU64_F32=1";
    memcpy(e_root, "AEMU64_ROOT=", 12); memcpy(e_root + 12, g_root, g_rootlen + 1);
    *a++ = e_root;
    if (g_props[0]) { memcpy(e_props, "AEMU64_PROPS=", 13); memcpy(e_props + 13, g_props, slen(g_props) + 1); *a++ = e_props; }
    if (g_bsock[0]) { memcpy(e_binder, "AEMU64_BINDER=", 14); memcpy(e_binder + 14, g_bsock, slen(g_bsock) + 1); *a++ = e_binder; }
    if (g_hwbsock[0]) { memcpy(e_hwbinder, "AEMU64_HWBINDER=", 16); memcpy(e_hwbinder + 16, g_hwbsock, slen(g_hwbsock) + 1); *a++ = e_hwbinder; }
    if (g_vndbsock[0]) { memcpy(e_vndbinder, "AEMU64_VNDBINDER=", 17); memcpy(e_vndbinder + 17, g_vndbsock, slen(g_vndbsock) + 1); *a++ = e_vndbinder; }
    char t[24], *s = utoa((u64)g_uid, t + sizeof t); u64 o = 10;
    memcpy(e_id, "AEMU64_ID=", 10); memcpy(e_id + o, s, slen(s)); o += slen(s); e_id[o++] = ',';
    s = utoa((u64)g_gid, t + sizeof t); memcpy(e_id + o, s, slen(s) + 1);
    *a++ = e_id; *a++ = e_f;
    if (g_crash) *a++ = "AEMU64_CRASH=1";
    static char e_input[130];
    if (g_input[0]) { memcpy(e_input, "AEMU64_INPUT=", 13); memcpy(e_input + 13, g_input, slen(g_input) + 1); *a++ = e_input; }
    *a++ = "AEMU64_F=1";
    static char e_k64[540];
    if (g_k64[0]) { memcpy(e_k64, "AEMU64_KERNEL64=", 16); memcpy(e_k64 + 16, g_k64, slen(g_k64) + 1); *a++ = e_k64; }
    { static char e_a32[540]; memcpy(e_a32, "AEMU64_ARM32=", 13); memcpy(e_a32 + 13, g_self, slen(g_self) + 1); *a++ = e_a32; }
    static char e_qemu[540];
    if (g_qemu[0]) { memcpy(e_qemu, "AEMU64_QEMU=", 12); memcpy(e_qemu + 12, g_qemu, slen(g_qemu) + 1); *a++ = e_qemu; }
    static char e_sd[540];
    if (g_sdlen) { memcpy(e_sd, "AEMU64_SDCARD=", 14); memcpy(e_sd + 14, g_sd, g_sdlen + 1); *a++ = e_sd; }
    *a++ = 0;
    r = S3(NR_execve, g_self, v, e);
    S2(NR_munmap, v, need);
    return r;
}

// ---------------------------------------------------------------------------------------------------- traps

// The trap handler runs on whatever stack the guest thread has, including a 16 KB alternate signal stack with
// the guest's own handler already on it. Path buffers therefore come from this pool, not from the stack.
#define NPB 48
static char g_pb[NPB][3 * PATH_MAX];
static volatile int g_pbbusy[NPB];

static char *pb_get(void) {
    for (;;) {
        for (int i = 0; i < NPB; i++) {
            int z = 0;
            if (!g_pbbusy[i] && __atomic_compare_exchange_n(&g_pbbusy[i], &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return g_pb[i];
        }
        S0(NR_sched_yield);
    }
}
static void pb_put(char *b) { __atomic_store_n(&g_pbbusy[(b - g_pb[0]) / (3 * PATH_MAX)], 0, __ATOMIC_RELEASE); }

static long unix_addr(long nr, long fd, const struct sockaddr_un *sa, u64 len, char *pb) {
    if (sa && len > 2 && sa->family == 1 && sa->path[0] == '/') {
        char *p = pb, *buf = pb + PATH_MAX; int err = 0;
        u64 n = len - 2; if (n > 108) n = 108;
        memcpy(p, sa->path, n); p[n] = 0;
        const char *h = xl(p, buf, &err);
        if (!h) return err;
        u64 hl = slen(h);
        if (hl >= 108) return -ENAMETOOLONG;
        struct sockaddr_un t; t.family = 1; memcpy(t.path, h, hl + 1);
        return S3(nr, fd, &t, 2 + hl + 1);
    }
    return S3(nr, fd, sa, len);
}

// On the phone every file of the image is the app's. Whom the guest gave a file under /data to (installd hands each
// app its data directory and on the next boot refuses one that reads as someone else's) is kept beside the file,
// in an attribute, and read back by stat.
#define OWN_ATTR "user.aemu.own"
static void own_put(long nr, long where, long uid, long gid) {        // nr: NR_lsetxattr / NR_setxattr / fsetxattr
    u32 v[2] = { 0, 0 };
    if ((u32)uid == 0xffffffffu || (u32)gid == 0xffffffffu) S4(nr == NR_fsetxattr ? NR_fgetxattr : nr + 3, where, OWN_ATTR, v, sizeof v);   // the matching get
    if ((u32)uid != 0xffffffffu) v[0] = (u32)uid;
    if ((u32)gid != 0xffffffffu) v[1] = (u32)gid;
    S5(nr, where, OWN_ATTR, v, sizeof v, 0);
}

#define P(i) ((const char *)a[i])
#define X(i) xl(P(i), b##i, &err)
#define NEEDX(i) const char *h##i = X(i); if (!h##i && P(i)) return err

// /system is a read-only partition on a phone. Here it is a directory of the app, and the guest runs as the app: its
// installd unlinked the precompiled code of the framework there (the only copy: the jars are stripped) when it could
// not replace it, and the firmware never started again. A change under /system is refused as a read-only file
// system refuses it. `h` is the host path of the call, or a name relative to `dirfd`; `t` is PATH_MAX of scratch.
#define EROFS 30
static int in_system(long dirfd, const char *h, char *t) {
    if (!h) return 0;
    if (h[0] != '/') {
        char l[40] = "/proc/self/fd/", num[24];
        if ((int)dirfd == AT_FDCWD) memcpy(l, "/proc/self/cwd", 15);
        else { char *d = utoa((u64)(u32)dirfd, num + sizeof num); memcpy(l + 14, d, slen(d) + 1); }
        long r = S4(NR_readlinkat, AT_FDCWD, l, t, PATH_MAX - 1);
        if (r <= 0) return 0;
        t[r] = 0;
        h = t;
        return pre(h, g_root) && (seq(h + g_rootlen, "/system") || pre(h + g_rootlen, "/system/"));
    }
    return pre(h, g_root) && pre(h + g_rootlen, "/system/");
}

static long trap2(long nr, long *a, void *uc, char **pbp);

static long trap(long nr, long *a, void *uc) {
    char *pb = 0;
    long r = trap2(nr, a, uc, &pb);
    if (pb) pb_put(pb);
    return r;
}

#define PB(i) ((*pbp ? *pbp : (*pbp = pb_get())) + (i) * PATH_MAX)

static long trap2(long nr, long *a, void *uc, char **pbp) {
    int err = 0;
    switch (nr) {               // legacy calls -> *at, then handled below
    case NR_open: { long b[6] = { AT_FDCWD, a[0], a[1], a[2], 0, 0 }; return trap2(NR_openat, b, uc, pbp); }
    case NR_access: { long b[6] = { AT_FDCWD, a[0], a[1], 0, 0, 0 }; return trap2(NR_faccessat, b, uc, pbp); }
    case NR_stat64: { long b[6] = { AT_FDCWD, a[0], a[1], 0, 0, 0 }; return trap2(NR_newfstatat, b, uc, pbp); }
    case NR_lstat64: { long b[6] = { AT_FDCWD, a[0], a[1], 0x100, 0, 0 }; return trap2(NR_newfstatat, b, uc, pbp); }
    case NR_readlink: { long b[6] = { AT_FDCWD, a[0], a[1], a[2], 0, 0 }; return trap2(NR_readlinkat, b, uc, pbp); }
    case NR_mkdir: { long b[6] = { AT_FDCWD, a[0], a[1], 0, 0, 0 }; return trap2(NR_mkdirat, b, uc, pbp); }
    case NR_rmdir: { long b[6] = { AT_FDCWD, a[0], 0x200, 0, 0, 0 }; return trap2(NR_unlinkat, b, uc, pbp); }
    case NR_unlink: { long b[6] = { AT_FDCWD, a[0], 0, 0, 0, 0 }; return trap2(NR_unlinkat, b, uc, pbp); }
    case NR_rename: { long b[6] = { AT_FDCWD, a[0], AT_FDCWD, a[1], 0, 0 }; return trap2(NR_renameat, b, uc, pbp); }
    case NR_chmod: { long b[6] = { AT_FDCWD, a[0], a[1], 0, 0, 0 }; return trap2(NR_fchmodat, b, uc, pbp); }
    case NR_chown32: { long b[6] = { AT_FDCWD, a[0], a[1], a[2], 0, 0 }; return trap2(NR_fchownat, b, uc, pbp); }
    case NR_lchown32: { long b[6] = { AT_FDCWD, a[0], a[1], a[2], 0x100, 0 }; return trap2(NR_fchownat, b, uc, pbp); }
    case NR_symlink: { long b[6] = { a[0], AT_FDCWD, a[1], 0, 0, 0 }; return trap2(NR_symlinkat, b, uc, pbp); }
    case NR_link: { long b[6] = { AT_FDCWD, a[0], AT_FDCWD, a[1], 0, 0 }; return trap2(NR_linkat, b, uc, pbp); }
    }
    switch (nr) {
    case NR_openat: {
        char *b1 = PB(0);
        if (P(1)) {
            if (P(1)[0] == '/' && P(1)[1] == 'd') {
                if (seq(P(1), "/dev/ashmem")) {
                    return S2(NR_memfd_create, "aemu-ashmem", (a[2] & 02000000) ? 1 : 0);
                }
                if (seq(P(1), "/dev/binder") || seq(P(1), "/dev/hwbinder") || seq(P(1), "/dev/vndbinder")) return binder_open(a[2], P(1));
                if (pre(P(1), "/dev/input/event")) return input_open(a[2]);
            }
            u64 pl = slen(P(1));
            if (pre(P(1), "/proc/") && ((pl >= 13 && seq(P(1) + pl - 13, "/attr/current")) || (pl >= 10 && seq(P(1) + pl - 10, "/attr/exec")))) {
                return fake_memfile("aemu-attr", "u:r:su:s0\n", 10);
            }
            // A process's command line starts with the runner (and the translator of a 32-bit one) on the phone; the
            // guest reads its own programs' names there (Google Play services checks which of its processes it is in).
            if (pre(P(1), "/proc/") && pl >= 14 && seq(P(1) + pl - 8, "/cmdline") && !(a[2] & 3)) {
                long f = S4(nr, AT_FDCWD, P(1), 0, 0);
                if (f >= 0) {
                    char t[4096]; long n = S3(NR_read, f, t, sizeof t - 1), o = 0;
                    S1(NR_close, f);
                    if (n < 0) n = 0;
                    t[n] = 0;
                    u64 l = slen(t);
                    if (l > 16 && (pre(t + l - 16, "/libaemu32_") || pre(t + l - 16, "/libaemu64_")) && seq(t + l - 3, ".so")) {
                        o = (long)l + 1;
                        if (o < n && g_qemu[0] && seq(t + o, g_qemu)) o += (long)slen(g_qemu) + 1;
                        if (o > n) o = n;
                    }
                    return fake_memfile("aemu-cmdline", t + o, (u64)(n - o));
                }
            }
            if (seq(P(1), "/sys/fs/selinux/enforce")) {
                return fake_memfile("aemu-enforce", "0\n", 2);
            }
        }
        NEEDX(1);
        if ((a[2] & (3 | 0100 | 01000)) && in_system(a[0], h1, PB(2))) return -EROFS;
        return S4(nr, a[0], h1, a[2], a[3]);
    }
    case NR_newfstatat: {
        // fstat as newer C libraries make it (qemu-arm's does): the same answers as for fstat
        if ((a[3] & 0x1000 /*AT_EMPTY_PATH*/) && P(1) && !P(1)[0]) { long f[6] = { a[0], a[2], 0, 0, 0, 0 }; return trap2(NR_fstat, f, uc, pbp); }
        char *b1 = PB(0);
        if (P(1)) {
            if (seq(P(1), "/dev/binder") || seq(P(1), "/dev/hwbinder") || seq(P(1), "/dev/vndbinder") || seq(P(1), "/dev/ashmem")) {
                struct kstat *st = (struct kstat *)a[2];
                memset(st, 0, sizeof *st);
                st->st_mode = 0020666; // S_IFCHR | 0666
                st->st_rdev = 0x102;
                st->st_nlink = 1;
                return 0;
            }
            u64 pl = slen(P(1));
            if (pre(P(1), "/proc/") && pl >= 13 && seq(P(1) + pl - 13, "/attr/current")) {
                struct kstat *st = (struct kstat *)a[2];
                memset(st, 0, sizeof *st);
                st->st_mode = 0100666; // S_IFREG | 0666
                st->st_size = 10;
                st->st_nlink = 1;
                return 0;
            }
            if (seq(P(1), "/sys/fs/selinux/enforce")) {
                struct kstat *st = (struct kstat *)a[2];
                memset(st, 0, sizeof *st);
                st->st_mode = 0100666;
                st->st_size = 2;
                st->st_nlink = 1;
                return 0;
            }
        }
        NEEDX(1);
        long r = S4(nr, a[0], h1, a[2], a[3]);
        if (r == 0) {
            struct kstat *st = (struct kstat *)a[2];
            fake_owner(st);
            // an app's own data belongs to the app (the class loader refuses an oat directory of another owner)
            u32 own[2];
            if (a[1] && pre(P(1), "/data/") && S4((a[3] & 0x100) ? NR_lgetxattr : NR_getxattr, h1, OWN_ATTR, own, sizeof own) == sizeof own) {
                st->st_uid = own[0]; st->st_gid = own[1];
            } else if (g_uid >= 10000 && st->st_uid == 0 && a[1] && (pre(P(1), "/data/data/") || pre(P(1), "/data/user/"))) {
                st->st_uid = (u32)g_uid; st->st_gid = (u32)g_gid;
            }
        }
        return r;
    }
    case NR_fstat: {
        if (is_binder_fd((int)a[0])) {
            struct kstat *st = (struct kstat *)a[1];
            memset(st, 0, sizeof *st);
            st->st_mode = 0020666; // S_IFCHR | 0666
            st->st_rdev = 0x101;
            st->st_nlink = 1;
            return 0;
        }
        long r = S2(nr, a[0], a[1]);
        if (r == 0) {
            struct kstat *st = (struct kstat *)a[1];
            if ((st->st_mode & 0170000) == 0100000 && !st->st_nlink && is_ashmem_fd((int)a[0])) {
                st->st_mode = 0020666; // S_IFCHR | 0666: libcutils checks that a region is the ashmem device
                st->st_rdev = 0x102;
                st->st_nlink = 1;
            }
            // the log as a pipe (and its copies, which qemu-arm keeps): zygote (8.0+) takes no pipes into a fork;
            // to it this is /dev/null, as on a phone
            if (g_logpipe && (st->st_mode & 0170000) == 0010000 && (st->st_ino == g_logino || st->st_ino == g_inino) && st->st_dev == g_logdev) {
                st->st_mode = 0020666; st->st_rdev = 0x103;
            }
            fake_owner(st);
        }
        return r;
    }
    case NR_faccessat: {
        char *b1 = PB(0);
        if (P(1)) {
            if (seq(P(1), "/dev/binder") || seq(P(1), "/dev/hwbinder") || seq(P(1), "/dev/vndbinder") || seq(P(1), "/dev/ashmem")) {
                return 0;
            }
            u64 pl = slen(P(1));
            if (pre(P(1), "/proc/") && pl >= 13 && seq(P(1) + pl - 13, "/attr/current")) return 0;
            if (seq(P(1), "/sys/fs/selinux/enforce")) return 0;
        }
        NEEDX(1);
        return S4(nr, a[0], h1, a[2], a[3]);
    }
    case NR_mkdirat: case NR_unlinkat: case NR_fchmodat: case NR_utimensat: case NR_mknodat:
    case NR_statx: {
        char *b1 = PB(0);
        NEEDX(1);
        if ((nr == NR_unlinkat || nr == NR_fchmodat) && in_system(a[0], h1, PB(2))) return -EROFS;
        return S5(nr, a[0], h1, a[2], a[3], a[4]);
    }
    case NR_fchown: own_put(NR_fsetxattr, a[0], a[1], a[2]); return 0;
    case NR_fchownat: {
        char *b1 = PB(0);
        if (P(1) && pre(P(1), "/data/")) {
            NEEDX(1);
            own_put((a[4] & 0x100) ? NR_lsetxattr : NR_setxattr, (long)h1, a[2], a[3]);
        }
        return 0;
    }
    case NR_readlinkat: {
        if (P(1)) {
            if (seq(P(1), "/proc/self/exe") && g_exe[0]) {
                u64 n = slen(g_exe); if (n > (u64)a[3]) n = (u64)a[3];
                memcpy((void *)a[2], g_exe, n); return (long)n;
            }
        }
        char *b1 = PB(0), *t = PB(1);
        NEEDX(1);
        long r = S4(nr, a[0], h1, t, PATH_MAX - 1);
        if (r < 0) return r;
        t[r] = 0;
        if (g_logpipe && pre(P(1), "/proc/") && pre(t, "pipe:")) { memcpy(t, "/dev/null", 10); r = 9; }
        if (pre(P(1), "/proc/") && t[0] == '/') {
            // what an open descriptor is: zygote (8.0+) lists its own before every fork and aborts on a file it does
            // not know. Its output is our log on the phone, outside the image: to the guest that is /dev/null.
            const char *q = P(1) + 6; while (*q && *q != '/') q++;
            if (pre(q, "/fd/")) {
                if (pre(t, "/memfd:aemu-ashmem")) memcpy(t, "/dev/ashmem", 12);
                else if (!(pre(t, g_root) && (t[g_rootlen] == '/' || !t[g_rootlen])) && !pre(t, "/dev/") &&
                         !(g_sdlen && pre(t, g_sd))) memcpy(t, "/dev/null", 10);
            }
        }
        unxl(t);
        u64 n = slen(t); if (n > (u64)a[3]) n = (u64)a[3];
        memcpy((void *)a[2], t, n);
        return (long)n;
    }
    case NR_symlinkat: {             // target, newdirfd, linkpath: an absolute target is re-rooted
        char *b0 = PB(0), *b2 = PB(1);
        NEEDX(0); NEEDX(2);
        return S3(nr, h0, a[1], h2);
    }
    case NR_linkat: case NR_renameat: case NR_renameat2: {
        char *b1 = PB(0), *b3 = PB(1);
        NEEDX(1); NEEDX(3);
        if (nr != NR_linkat && (in_system(a[0], h1, PB(2)) || in_system(a[2], h3, PB(2)))) return -EROFS;
        return S5(nr, a[0], h1, a[2], h3, a[4]);
    }
    case NR_statfs: case NR_truncate: case NR_chdir:
    case NR_setxattr: case NR_lsetxattr: case NR_getxattr: case NR_lgetxattr: case NR_listxattr: case NR_llistxattr:
    case NR_removexattr: case NR_lremovexattr: {
        char *b0 = PB(0);
        NEEDX(0);
        long r = S5(nr, h0, a[1], a[2], a[3], a[4]);
        if (nr == NR_statfs && r == 0) {
            // a nearly full phone reads as a nearly full guest disk (no dropbox, low-storage modes): the guest's disk
            // is what is still free plus as much again
            u64 *s = (u64 *)((char *)a[2] + 8); /* statfs64: type, bsize, then blocks, bfree, bavail */
            if (s[2] && s[0] > 2 * s[2]) { s[0] = 2 * s[2]; if (s[1] > s[0]) s[1] = s[0]; }
        }
        return r;
    }
    case NR_inotify_add_watch: {
        char *b1 = PB(0);
        NEEDX(1);
        return S3(nr, a[0], h1, a[2]);
    }
    case NR_getcwd: {
        char *t = PB(0);
        long r = S2(nr, t, PATH_MAX);
        if (r < 0) return r;
        unxl(t);
        u64 n = slen(t) + 1;
        if (n > (u64)a[1]) return -34; /* ERANGE */
        memcpy((void *)a[0], t, n);
        return (long)n;
    }
    case NR_bind: case NR_connect:
        if (is_nl_fd((int)a[0])) return 0;
        if (a[1] && *(u16 *)a[1] == 16) {           // a real netlink socket: binding to groups is not for an app
            long r = S3(nr, a[0], a[1], a[2]);
            return r < 0 && nr == NR_bind ? 0 : r;
        }
        return unix_addr(nr, a[0], (const struct sockaddr_un *)a[1], (u64)a[2], PB(0));
    case NR_getsockname: {
        long r = S3(nr, a[0], a[1], a[2]);
        if (r == 0 && a[1] && a[2] && *(u32 *)a[2] > 2) {
            // a service's socket lives in the run directory as <name>.sock; the guest made it as /dev/socket/<name>
            struct sockaddr_un *sa = (struct sockaddr_un *)a[1];
            u64 l = sa->family == 1 && sa->path[0] ? slen(sa->path) : 0;
            if (l > 5 && seq(sa->path + l - 5, ".sock")) {
                u64 b = l - 5; while (b && sa->path[b - 1] != '/') b--;
                char name[64]; u64 nl = l - 5 - b;
                if (nl < sizeof name) {
                    memcpy(name, sa->path + b, nl); name[nl] = 0;
                    memcpy(sa->path, "/dev/socket/", 12); memcpy(sa->path + 12, name, nl + 1);
                    *(u32 *)a[2] = (u32)(2 + 12 + nl + 1);
                }
            } else if (l) {
                unxl(sa->path);
                *(u32 *)a[2] = (u32)(2 + slen(sa->path) + 1);
            }
        }
        return r;
    }
    case NR_dup3: {
        // zygote's children put /dev/null back over every descriptor that "was /dev/null": the log stays where it is
        if (g_logino && a[0] != a[1]) {
            struct kstat st;
            if (S2(NR_fstat, a[1], &st) == 0 && st.st_ino == g_logino && st.st_dev == g_logdev &&
                S2(NR_fstat, a[0], &st) == 0 && (st.st_mode & 0170000) == 0020000 && st.st_rdev == 0x103) return a[1];
        }
        return S3(nr, a[0], a[1], a[2]);
    }
    case NR_close: {
        if (is_nl_fd((int)a[0])) nl_close((int)a[0]);
        return S1(nr, a[0]);
    }
    case NR_execve:
        return do_exec(P(0), (char *const *)a[1], (char *const *)a[2]);
    case NR_ioctl:
        if (((a[1] >> 8) & 0xff) == ASHMEM_MAGIC) return ashmem_ioctl((int)a[0], (u32)a[1], (u64)a[2]);
        if (((a[1] >> 8) & 0xff) == 'E' && is_input_fd((int)a[0])) return input_ioctl((u32)a[1], (u64)a[2]);
        if (((u32)a[1] & 0xffffff00u) == 0x4600) return fb_ioctl((u32)a[1], (u64)a[2]);
        if (((a[1] >> 8) & 0xff) == 'b' && is_binder_fd((int)a[0])) return binder_ioctl((int)a[0], (u32)a[1], (u64)a[2]);
        return S3(nr, a[0], a[1], a[2]);
    case NR_mmap:
        if (a[2] == 1 && is_binder_fd((int)a[4])) return binder_mmap((u64)a[0], (u64)a[1], a[3]);
        if (a[2] == 1 && g_props[0] && (int)a[4] >= 0 && !gles2_for_32()) {
            // 32-bit programs only have the GLES 1 renderer: in their (private) view of the property area the
            // "GLES 2 is there" flag reads 0, so their windows are drawn without hwui. Only that page is a copy.
            static struct kstat ps; struct kstat fs;
            if (!ps.st_ino) S4(NR_newfstatat, AT_FDCWD, g_props, &ps, 0);
            if (ps.st_ino && S2(NR_fstat, a[4], &fs) == 0 && fs.st_ino == ps.st_ino && fs.st_dev == ps.st_dev) {
                long r = S6(nr, a[0], a[1], 3, (a[3] & ~1L) | 2, a[4], a[5]);
                if (r < 0 && r > -4096) return S6(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
                static const char *const key[] = { "ro.kernel.qemu.gles", "qemu.gles", 0 };   // libEGL's and hwui's
                char *m = (char *)r; u64 len = (u64)a[1];
                for (int k = 0; key[k]; k++)
                    for (u64 i = 96; i + 24 <= len; i += 4)
                        if (m[i] == key[k][0] && seq(m + i, key[k])) { if ((m[i - 92] == '1' || m[i - 92] == '2') && !m[i - 91]) m[i - 92] = '0'; break; }
                S3(NR_mprotect, r, a[1], 1);
                return r;
            }
        }
        return S6(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
    case NR_mount: case NR_umount2: case NR_chroot: case NR_capset: case NR_setgroups: case NR_unshare: return 0;
    case NR_prctl: return 0;                    // only PR_CAPBSET_DROP gets here
    case NR_timerfd_create: {                   // the *_ALARM clocks need CAP_WAKE_ALARM, vendor clocks (power-off alarm)
        long r = S2(nr, a[0], a[1]);            // do not exist here: the plain clocks tick the same
        if (r < 0) r = S2(nr, a[0] == 9 ? 7 : 0, a[1]);
        return r;
    }
    case NR_reboot: S1(NR_exit_group, 0); return 0;
    case NR_capget: {
        struct { u32 e, p, i; } *d = (void *)a[1];
        if (d) { d[0].e = d[0].p = 0xffffffff; d[0].i = 0; d[1].e = d[1].p = 0xffffffff; d[1].i = 0; }
        return 0;
    }
    case NR_getuid: case NR_geteuid: return g_uid;
    case NR_getgid: case NR_getegid: return g_gid;
    case NR_setuid: g_uid = (int)a[0]; return 0;
    case NR_setgid: g_gid = (int)a[0]; return 0;
    case NR_setreuid: case NR_setresuid: if ((int)a[1] != -1) g_uid = (int)a[1]; else if ((int)a[0] != -1) g_uid = (int)a[0]; return 0;
    case NR_setregid: case NR_setresgid: if ((int)a[1] != -1) g_gid = (int)a[1]; else if ((int)a[0] != -1) g_gid = (int)a[0]; return 0;
    case NR_setfsuid: return g_uid;
    case NR_setfsgid: return g_gid;
    case NR_getresuid: for (int i = 0; i < 3; i++) if (a[i]) *(u32 *)a[i] = (u32)g_uid; return 0;
    case NR_getresgid: for (int i = 0; i < 3; i++) if (a[i]) *(u32 *)a[i] = (u32)g_gid; return 0;
    case NR_getgroups: return 0;
    case NR_rt_sigprocmask: {
        // We are inside a signal handler: the mask that counts is the one the kernel restores on return. SIGSYS
        // stays deliverable whatever the guest asks, or the kernel would kill the process on the next trap.
        u64 *mask = (u64 *)((char *)uc + 104), old = *mask;
        if (a[1]) {
            u64 set = *(u64 *)a[1];
            if (a[0] == 0) *mask = old | set; else if (a[0] == 1) *mask = old & ~set; else if (a[0] == 2) *mask = set; else return -EINVAL;
            *mask &= ~(u64)(SYSBIT | (1UL << 8) | (1UL << 18));     // and never SIGKILL, SIGSTOP
            if (g_crash) *mask &= ~(u64)((1UL << 10) | (1UL << 6) | (1UL << 3) | (1UL << 5) | (1UL << 7));
        }
        if (a[2]) *(u64 *)a[2] = old;
        return 0;
    }
    case NR_socket: {
        // netlink: routing (protocol 0) is the phone's own while it lets an app have it — the kernel answers every
        // request, mostly with a refusal. The rest (uevents, nflog, netfilter, nl80211) is closed to an app: those
        // get a socket that answers by itself.
        long r = a[0] == 16 && a[2] != 0 ? -1 : S3(nr, a[0], a[1], a[2]);
        if (r < 0 && a[0] == 16) {
            int sv[2];
            if (S4(NR_socketpair, 1 /*AF_UNIX*/, 2 /*SOCK_DGRAM*/ | (a[1] & 02004000), 0, (long)sv) == 0) {
                nl_register(sv[0], sv[1]);
                return sv[0];
            }
        }
        return r;
    }
    case NR_setsockopt: {
        if (is_nl_fd((int)a[0])) return 0;
        long r = S5(nr, a[0], a[1], a[2], a[3], a[4]);
        if (r < 0 && a[1] == 1 && (a[2] == 33 || a[2] == 32)) {    // SO_RCVBUFFORCE / SO_SNDBUFFORCE need CAP_NET_ADMIN
            S5(nr, a[0], 1, a[2] == 33 ? 8 : 7, a[3], a[4]);
            return 0;
        }
        if (r < 0 && a[1] == 1 && a[2] == 36) return 0;             // SO_MARK (netd tags every app socket): CAP_NET_ADMIN too
        return r;
    }
    case NR_getsockopt: {
        if (is_nl_fd((int)a[0])) return 0;
        return S5(nr, a[0], a[1], a[2], a[3], a[4]);
    }
    case NR_sendto: {
        if (is_nl_fd((int)a[0])) {
            nl_handle_send((int)a[0], (const void *)a[1], (u64)a[2]);
            return a[2];
        }
        return S6(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    case NR_sendmsg: {
        if (is_nl_fd((int)a[0])) {
            struct msgh *m = (struct msgh *)a[1];
            if (m && m->iov && m->iovlen > 0) {
                nl_handle_send((int)a[0], m->iov[0].base, m->iov[0].len);
                u64 total = 0;
                for (u64 i = 0; i < m->iovlen; i++) total += m->iov[i].len;
                return (long)total;
            }
            return 0;
        }
        return S3(nr, a[0], a[1], a[2]);
    }
    case NR_rt_sigaction: {
        struct ksa { void *h; ul flags; void *restorer; u32 mask[2]; } t;
        if (a[0] == SIGSYS) { if (a[2]) memset((void *)a[2], 0, sizeof t); return 0; }
        if (is_fatal(a[0])) {
            struct ksa old; void *prev = (void *)g_fatal[a[0]];
            long r;
            if (a[1]) {
                t = *(struct ksa *)a[1]; t.mask[0] &= ~SYSBIT;
                void *want = t.h;
                if ((ul)want != 1) { t.h = (void *)fatal_relay; t.flags |= 4; if (!want) t.flags &= ~0x80000000UL; }   // not SA_RESETHAND for our own
                r = S4(nr, a[0], &t, &old, a[3]);
                if (r == 0) g_fatal[a[0]] = want;
            } else r = S4(nr, a[0], 0, &old, a[3]);
            if (r == 0 && a[2]) { if (old.h == (void *)fatal_relay) old.h = prev; *(struct ksa *)a[2] = old; }
            return r;
        }
        if (!a[1]) return S4(nr, a[0], 0, a[2], a[3]);
        t = *(struct ksa *)a[1]; t.mask[0] &= ~SYSBIT;
        return S4(nr, a[0], &t, a[2], a[3]);
    }
    }
    return -ENOSYS;
}

// AEMU64_CRASH=1: fatal signals are reported here (with the mapping of pc and lr) instead of the guest's own handlers

// ---------------------------------------------------------------------------------------------------- crashes

static void where(ul addr) {
    static char buf[1 << 16];
    long fd = S4(NR_openat, AT_FDCWD, "/proc/self/maps", 0, 0);
    if (fd < 0) return;
    ul keep = 0; long n;
    while ((n = S3(NR_read, fd, buf + keep, sizeof buf - 1 - keep)) > 0) {
        ul len = keep + (ul)n; buf[len] = 0;
        char *l = buf;
        for (;;) {
            char *e = l; while (*e && *e != '\n') e++;
            if (!*e) break;
            *e = 0;
            ul lo = 0, hi = 0; char *q = l;
            for (; *q && *q != '-'; q++) lo = lo * 16 + (ul)(*q <= '9' ? *q - '0' : *q - 'a' + 10);
            if (*q) for (q++; *q && *q != ' '; q++) hi = hi * 16 + (ul)(*q <= '9' ? *q - '0' : *q - 'a' + 10);
            if (addr >= lo && addr < hi) { say("    "); say(l); say("  +"); hex(addr - lo); say("\n"); S1(NR_close, fd); return; }
            l = e + 1;
        }
        keep = slen(l); memmove(buf, l, keep);
    }
    S1(NR_close, fd);
}

// ARM ucontext: uc_mcontext at 20 (trap_no, error_code, oldmask, r0..r10, fp, ip, sp, lr, pc, cpsr, fault_address),
// uc_sigmask at 104
#define UC_REGS(uc) ((ul *)((char *)(uc) + 32))     /* r0..r15, then cpsr */
#define UC_MASK(uc) ((u32 *)((char *)(uc) + 104))

static void on_crash(int sig, void *info, void *uc) {
    ul *r = UC_REGS(uc);
    char t[24];
    static volatile int busy;
    while (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE)) S0(NR_sched_yield);
    {
        static char lp[600];
        memcpy(lp, g_root, g_rootlen); memcpy(lp + g_rootlen, "/dev/aemu_crash.log", 20);
        g_sayfd = (int)S4(NR_openat, AT_FDCWD, lp, 02101 /*O_WRONLY|O_CREAT|O_APPEND*/, 0600);
    }
    say("aemu32: fatal signal "); say(utoa((u64)sig, t + sizeof t)); say(" in "); say(g_exe);
    { char nm[20]; memset(nm, 0, sizeof nm); S5(NR_prctl, 16 /*PR_GET_NAME*/, nm, 0, 0, 0); say(" ["); say(nm); say("]"); }
    say("\n  pc "); hex(r[15]); say("  lr "); hex(r[14]); say("  addr "); hex(*(ul *)((char *)info + 12)); say("\n");
    where(r[15]); where(r[14]);
    for (int i = 0; i < 13; i++) { say("  r"); say(utoa((u64)i, t + sizeof t)); say(" "); hex(r[i]); if (i % 4 == 3) say("\n"); }
    say("\n  sp "); hex(r[13]); say("  pid "); say(utoa((u64)S0(NR_getpid), t + sizeof t)); say("\n");
    S1(NR_exit_group, 128 + sig);
}

static void fatal_relay(int sig, void *info, void *uc) {
    if (g_crash) { on_crash(sig, info, uc); return; }
    void (*h)(int, void *, void *) = g_fatal[sig & 31];
    if ((ul)h > 1) h(sig, info, uc); else on_crash(sig, info, uc);
}

static void on_sigsys(int sig, void *info, void *uc) {
    ul *r = UC_REGS(uc);
    (void)sig; (void)info;
    if (g_crash) *UC_MASK(uc) &= ~((1U << 10) | (1U << 6) | (1U << 3) | (1U << 5) | (1U << 7));
    long a[6] = { (long)r[0], (long)r[1], (long)r[2], (long)r[3], (long)r[4], (long)r[5] };
    r[0] = (ul)trap((long)r[7], a, uc);
}

// ---------------------------------------------------------------------------------------------------- filter

struct bpf { u16 code; u8 jt, jf; u32 k; };
#define LD(off) (struct bpf){ 0x20, 0, 0, off }
#define JEQ(k, t, f) (struct bpf){ 0x15, t, f, k }
#define JGE(k, t, f) (struct bpf){ 0x35, t, f, k }
#define RET(k) (struct bpf){ 0x06, 0, 0, k }
#define ALLOW 0x7fff0000u
#define TRAP 0x00030000u

static const u16 trapped[] = {
    NR_setxattr, NR_lsetxattr, NR_getxattr, NR_lgetxattr, NR_listxattr, NR_llistxattr, NR_removexattr, NR_lremovexattr,
    NR_getcwd, NR_inotify_add_watch, NR_ioctl, NR_mknodat, NR_mkdirat, NR_unlinkat, NR_symlinkat, NR_linkat,
    NR_renameat, NR_umount2, NR_mount, NR_statfs, NR_truncate, NR_faccessat, NR_chdir, NR_chroot, NR_fchmodat,
    NR_fchownat, NR_fchown, NR_openat, NR_readlinkat, NR_newfstatat, NR_fstat, NR_utimensat, NR_capget, NR_capset,
    NR_rt_sigaction, NR_reboot, NR_setregid, NR_setgid, NR_setreuid, NR_setuid, NR_setresuid, NR_getresuid,
    NR_setresgid, NR_getresgid, NR_setfsuid, NR_setfsgid, NR_getgroups, NR_setgroups, NR_getuid, NR_geteuid,
    NR_getgid, NR_getegid, NR_bind, NR_connect, NR_execve, NR_setsockopt, NR_unshare, NR_timerfd_create,
    NR_close, NR_getsockopt, NR_sendto, NR_sendmsg, NR_getsockname, NR_socket, NR_dup3, NR_renameat2, NR_statx,
    NR_open, NR_access, NR_stat64, NR_lstat64, NR_readlink, NR_mkdir, NR_rmdir, NR_unlink, NR_rename, NR_chmod,
    NR_chown32, NR_lchown32, NR_symlink, NR_link,
};

enum { L_TRAP = 240, L_ALLOW, L_SIG, L_MMAP, L_NR, L_PRCTL, L_N };

// stacked on the arm64 kernel's filter, which lets every call of another architecture through
static void install_filter(void) {
    static struct bpf f[200];
    int n = 0, pos[L_N - 240];
#define AT(l) pos[(l) - 240] = n
    f[n++] = LD(4);  f[n++] = JEQ(0x40000028u /*AUDIT_ARCH_ARM*/, 0, L_ALLOW);
    f[n++] = LD(8);  f[n++] = JGE(IMAGE_BASE, 0, L_NR);             // calls made from this image pass
    f[n++] = JGE(IMAGE_BASE + IMAGE_SPAN, L_NR, L_ALLOW);
    AT(L_NR); f[n++] = LD(0);
    for (ul i = 0; i < sizeof trapped / sizeof trapped[0]; i++) f[n++] = JEQ(trapped[i], L_TRAP, 0);
    f[n++] = JEQ(NR_rt_sigprocmask, L_SIG, 0);
    f[n++] = JEQ(NR_prctl, L_PRCTL, 0);
    f[n++] = JEQ(NR_mmap, L_MMAP, L_ALLOW);
    AT(L_PRCTL);
    f[n++] = LD(16); f[n++] = JEQ(24, L_TRAP, 0);
    f[n++] = JEQ(22, L_TRAP, 0);
    f[n++] = JEQ(8, L_TRAP, 0);
    f[n++] = JEQ(47, L_TRAP, L_ALLOW);
    AT(L_SIG);
    f[n++] = LD(16); f[n++] = JEQ(1, L_ALLOW, 0);
    f[n++] = LD(24); f[n++] = JEQ(0, L_ALLOW, L_TRAP);
    AT(L_MMAP);
    f[n++] = LD(32); f[n++] = JEQ(1, 0, L_ALLOW);
    f[n++] = LD(48); f[n++] = JEQ(0xffffffffu, L_ALLOW, L_TRAP);
    AT(L_TRAP); f[n++] = RET(TRAP);
    AT(L_ALLOW); f[n++] = RET(ALLOW);
#undef AT
    for (int i = 0; i < n; i++) {
        if ((f[i].code & 7) != 5) continue;
        if (f[i].jt >= 240) f[i].jt = (u8)(pos[f[i].jt - 240] - i - 1);
        if (f[i].jf >= 240) f[i].jf = (u8)(pos[f[i].jf - 240] - i - 1);
    }
    struct { u16 len; struct bpf *f; } prog = { (u16)n, f };
    S5(NR_prctl, 38 /*PR_SET_NO_NEW_PRIVS*/, 1, 0, 0, 0);
    if (S3(NR_prctl, 22 /*PR_SET_SECCOMP*/, 2, &prog) < 0) die("seccomp filter refused", "");
}

// ---------------------------------------------------------------------------------------------------- loader

struct ehdr { u8 ident[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct phdr { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct image { ul bias, entry, phdr; u16 phnum; char interp[256]; };

#define PG 4096UL
#define DN(x) ((x) & ~(PG - 1))
#define UP(x) DN((x) + PG - 1)

static long pread32(long fd, void *b, ul n, ul off) { return sc(NR_pread64, fd, (long)b, (long)n, 0, (long)off, 0); }
static ul mmap32(ul a, ul len, long prot, long fl, long fd, ul off) { return (ul)S6(NR_mmap, a, len, prot, fl, fd, off / PG); }

static int load_elf(const char *host, struct image *im) {
    long fd = S4(NR_openat, AT_FDCWD, host, 0x80000 /*O_CLOEXEC*/, 0);
    if (fd < 0) return (int)fd;
    struct ehdr eh; struct phdr ph[32];
    if (pread32(fd, &eh, sizeof eh, 0) != sizeof eh || eh.ident[0] != 0x7f || eh.ident[1] != 'E' ||
        eh.ident[4] != 1 || eh.machine != 40 /*EM_ARM*/ || eh.phnum > 32) { S1(NR_close, fd); return -ENOEXEC; }
    if (pread32(fd, ph, eh.phnum * sizeof ph[0], eh.phoff) != (long)(eh.phnum * sizeof ph[0])) { S1(NR_close, fd); return -ENOEXEC; }
    ul lo = ~0UL, hi = 0;
    im->interp[0] = 0; im->phdr = 0;
    for (int i = 0; i < eh.phnum; i++) {
        if (ph[i].type == 1) { if (ph[i].vaddr < lo) lo = ph[i].vaddr; if (ph[i].vaddr + ph[i].memsz > hi) hi = ph[i].vaddr + ph[i].memsz; }
        if (ph[i].type == 3 && ph[i].filesz < sizeof im->interp) { pread32(fd, im->interp, ph[i].filesz, ph[i].offset); im->interp[ph[i].filesz] = 0; }
    }
    lo = DN(lo); hi = UP(hi);
    ul base = mmap32(eh.type == 3 ? 0 : lo, hi - lo, 0, 0x22 | (eh.type == 3 ? 0 : 0x10), -1, 0);
    if (base >= (ul)-4095) { S1(NR_close, fd); return -ENOMEM; }
    ul bias = base - lo;
    for (int i = 0; i < eh.phnum; i++) {
        if (ph[i].type != 1) continue;
        int prot = ((ph[i].flags & 4) ? 1 : 0) | ((ph[i].flags & 2) ? 2 : 0) | ((ph[i].flags & 1) ? 4 : 0);
        ul s = DN(ph[i].vaddr) + bias, fe = ph[i].vaddr + ph[i].filesz + bias, me = UP(ph[i].vaddr + ph[i].memsz + bias);
        if (ph[i].filesz) {
            ul r = mmap32(s, UP(fe) - s, prot, 0x12 /*PRIVATE|FIXED*/, fd, DN(ph[i].offset));
            if (r >= (ul)-4095) { S1(NR_close, fd); return (int)r; }
        }
        if (ph[i].memsz > ph[i].filesz) {
            if ((prot & 2) && UP(fe) > fe) memset((void *)fe, 0, UP(fe) - fe);
            if (me > UP(fe)) mmap32(UP(fe), me - UP(fe), prot, 0x32, -1, 0);
        }
        if (ph[i].offset <= eh.phoff && eh.phoff < ph[i].offset + ph[i].filesz) im->phdr = ph[i].vaddr + (eh.phoff - ph[i].offset) + bias;
    }
    S1(NR_close, fd);
    im->bias = bias; im->entry = eh.entry + bias; im->phnum = eh.phnum;
    return 0;
}
static const char *env_get(char **envp, const char *k) {
    ul n = slen(k);
    for (; *envp; envp++) if (pre(*envp, k) && (*envp)[n] == '=') return *envp + n + 1;
    return 0;
}

// ---------------------------------------------------------------------------------------------------- main


void aemu_main(ul *sp) {
    long argc = (long)sp[0];
    char **argv = (char **)(sp + 1), **envp = argv + argc + 1;
    ul *auxv; { char **e = envp; while (*e) e++; auxv = (ul *)(e + 1); }
    if (argc < 3) die("usage: aemu32 <argv0> <guest program> [args...]", "");

    const char *r = env_get(envp, "AEMU64_ROOT");
    if (!r || !r[0] || slen(r) >= sizeof g_root) die("AEMU64_ROOT is not set", "");
    memcpy(g_root, r, slen(r) + 1); g_rootlen = slen(g_root);
    if ((r = env_get(envp, "AEMU64_PROPS")) && slen(r) < sizeof g_props) memcpy(g_props, r, slen(r) + 1);
    g_crash = env_get(envp, "AEMU64_CRASH") != 0;
    if ((r = env_get(envp, "AEMU64_INPUT")) && slen(r) < sizeof g_input) memcpy(g_input, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_SDCARD")) && slen(r) < sizeof g_sd) { g_sdlen = slen(r); memcpy(g_sd, r, g_sdlen + 1); }
    if ((r = env_get(envp, "AEMU64_QEMU")) && slen(r) < sizeof g_qemu) memcpy(g_qemu, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_KERNEL64")) && slen(r) < sizeof g_k64) memcpy(g_k64, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_BINDER")) && slen(r) < sizeof g_bsock) memcpy(g_bsock, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_HWBINDER")) && slen(r) < sizeof g_hwbsock) memcpy(g_hwbsock, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_VNDBINDER")) && slen(r) < sizeof g_vndbsock) memcpy(g_vndbsock, r, slen(r) + 1);
    if ((r = env_get(envp, "AEMU64_ID"))) { g_uid = (int)atou(r); while (*r && *r != ',') r++; if (*r) g_gid = (int)atou(r + 1); }
    long n = S4(NR_readlinkat, AT_FDCWD, "/proc/self/exe", g_self, sizeof g_self - 1);
    if (n <= 0) die("cannot find myself", "");
    g_self[n] = 0;
    g_real_uid = (u32)S0(NR_getuid);
    { struct kstat st; if (S2(NR_fstat, 2, &st) == 0 && ((st.st_mode & 0170000) == 0100000 || (st.st_mode & 0170000) == 0010000)) {
        g_logdev = st.st_dev; g_logino = st.st_ino; g_logpipe = (st.st_mode & 0170000) == 0010000; }
      if (g_logpipe && S2(NR_fstat, 0, &st) == 0 && (st.st_mode & 0170000) == 0010000 && st.st_dev == g_logdev) g_inino = st.st_ino; }

    char pbuf[PATH_MAX]; int err = 0;
    const char *prog = xl(argv[2], pbuf, &err);
    if (!prog) die("bad path ", argv[2]);

    char head[256];
    long fd = S4(NR_openat, AT_FDCWD, prog, 0x80000, 0);
    if (fd < 0) die("cannot open ", argv[2]);
    long hn = S3(NR_read, fd, head, sizeof head - 1);
    S1(NR_close, fd);
    static char *nargv[4100];
    char **gargv = argv + 2; long gargc = argc - 2;
    const char *argv0 = argv[1];
    if (hn > 2 && head[0] == '#' && head[1] == '!') {
        head[hn] = 0;
        char *p = head + 2, *arg = 0; while (*p == ' ') p++;
        char *interp = p; while (*p && *p != ' ' && *p != '\n') p++;
        if (*p == ' ') { *p++ = 0; while (*p == ' ') p++; if (*p && *p != '\n') { arg = p; while (*p && *p != '\n') p++; } }
        *p = 0;
        static char ibuf[PATH_MAX], script[PATH_MAX];
        memcpy(script, argv[2], slen(argv[2]) + 1); unxl(script);
        long k = 0;
        static char ihold[256]; memcpy(ihold, interp, slen(interp) + 1);
        nargv[k++] = ihold;
        if (arg) { static char ahold[256]; memcpy(ahold, arg, slen(arg) + 1); nargv[k++] = ahold; }
        nargv[k++] = script;
        for (long i = 3; i < argc && k < 4096; i++) nargv[k++] = argv[i];
        nargv[k] = 0; gargv = nargv; gargc = k; argv0 = ihold;
        if (!(prog = xl(ihold, ibuf, &err))) die("bad interpreter ", ihold);
        memcpy(g_exe, ihold, slen(ihold) + 1);
        long f2 = S4(NR_openat, AT_FDCWD, prog, 0x80000, 0);
        hn = f2 >= 0 ? S3(NR_read, f2, head, 8) : 0;
        if (f2 >= 0) S1(NR_close, f2);
    } else {
        ul l = slen(prog); if (l >= sizeof g_exe) l = sizeof g_exe - 1;
        memcpy(g_exe, prog, l); g_exe[l] = 0; unxl(g_exe);
    }
    // a 64-bit program (a 32-bit shell starting one): the arm64 kernel runs it, with the same arguments
    if (hn > 5 && head[0] == 0x7f && head[4] == 2) {
        if (!g_k64[0]) die("64-bit program and no arm64 kernel: ", prog);
        argv[0] = g_k64;
        S3(NR_execve, g_k64, argv, envp);
        die("cannot start the arm64 kernel for ", prog);
    }

    static char sockenv[12][80]; int nsock = 0;
    for (char **e = envp; *e; e++) {
        if (!pre(*e, "AEMU64_SOCK_") || nsock >= 12) continue;
        const char *name = *e + 12, *eq = name; while (*eq && *eq != '=') eq++;
        if (!*eq) continue;
        struct sockaddr_un sa; sa.family = 1;
        ul pl = 0; const char *v = eq + 1; while (v[pl] && v[pl] != ',' && pl < 107) { sa.path[pl] = v[pl]; pl++; }
        sa.path[pl] = 0;
        ul mode = 0; if (v[pl] == ',') for (const char *m = v + pl + 1; *m >= '0' && *m <= '7'; m++) mode = mode * 8 + (ul)(*m - '0');
        long s = S3(NR_socket, 1, 1 /*SOCK_STREAM*/, 0);
        if (s < 0) continue;
        S3(NR_unlinkat, AT_FDCWD, sa.path, 0);
        if (S3(NR_bind, s, &sa, 2 + pl + 1) < 0) { S1(NR_close, s); continue; }
        S4(NR_fchmodat, AT_FDCWD, sa.path, mode ? mode : 0660, 0);
        char *o = sockenv[nsock++]; ul nl = (ul)(eq - name); if (nl > 40) nl = 40;
        memcpy(o, "ANDROID_SOCKET_", 15); memcpy(o + 15, name, nl); o[15 + nl] = '=';
        char t[24], *d = utoa((u64)s, t + sizeof t); memcpy(o + 16 + nl, d, slen(d) + 1);
    }
    {
        const char *b = g_exe, *q;
        for (q = g_exe; *q; q++) if (*q == '/') b = q + 1;
        static const char *const mute[] = { "ip", "tc", "iptables", "ip6tables", "iptables-restore", "ip6tables-restore", "ndppd", 0 };
        for (int i = 0; mute[i]; i++) if (seq(b, mute[i])) S1(NR_exit_group, 0);
    }
    { struct kstat st; if (S4(NR_newfstatat, AT_FDCWD, prog, &st, 0) == 0 && st.st_size < 1024) S1(NR_exit_group, 0); }   // the engine's "true"

    static char ldpath[] = "LD_LIBRARY_PATH=/vendor/lib:/system/lib";
    for (char **e = envp; *e; e++) if (pre(*e, "LD_LIBRARY_PATH=")) *e = ldpath;
    struct image exe, ld;
    if ((err = load_elf(prog, &exe))) die("cannot load ", prog);
    ul entry = exe.entry, base = 0;
    if (exe.interp[0]) {
        char ib[PATH_MAX];
        const char *ih = xl(exe.interp, ib, &err);
        if (!ih || (err = load_elf(ih, &ld))) die("cannot load the linker ", exe.interp);
        entry = ld.entry; base = ld.bias;
    }

    // ARM rt_sigaction: handler, flags, restorer, mask (two words)
    struct { void *h; ul flags; void *restorer; u32 mask[2]; } sa = { (void *)on_sigsys, 4 /*SA_SIGINFO*/ | 0x40000000 /*SA_NODEFER*/, 0, { 0, 0 } };
    if (S4(NR_rt_sigaction, SIGSYS, &sa, 0, 8) < 0) die("sigaction", "");
    u32 unblock[2] = { SYSBIT, 0 }; S4(NR_rt_sigprocmask, 1, unblock, 0, 8);
    {
        struct { void *h; ul flags; void *restorer; u32 mask[2]; } ca = { (void *)fatal_relay, 4 | 0x40000000, 0, { 0, 0 } };
        static const int fatal[] = { 11, 7, 4, 6, 8 };
        for (int i = 0; i < 5; i++) S4(NR_rt_sigaction, fatal[i], &ca, 0, 8);
    }
    if (!env_get(envp, "AEMU64_F32")) install_filter();

    ul ne = 0; while (envp[ne]) ne++;
    ul na = 0; while (auxv[na * 2]) na++;
    ul words = 1 + (ul)gargc + 1 + ne + 12 + 1 + (na + 1) * 2;
    ul *w = (ul *)(((ul)&words - (256UL << 10) - words * 4) & ~15UL), *top = w;
    *w++ = (ul)gargc;
    *w++ = (ul)argv0;
    for (long i = 1; i < gargc; i++) *w++ = (ul)gargv[i];
    *w++ = 0;
    for (ul i = 0; i < ne; i++) if (!env_is_ours(envp[i])) *w++ = (ul)envp[i];
    for (int i = 0; i < nsock; i++) *w++ = (ul)sockenv[i];
    *w++ = 0;
    for (ul i = 0; i < na; i++) {
        ul t = auxv[i * 2], v = auxv[i * 2 + 1];
        switch (t) {
        case 3: v = exe.phdr; break;            // AT_PHDR
        case 4: v = sizeof(struct phdr); break; // AT_PHENT
        case 5: v = exe.phnum; break;           // AT_PHNUM
        case 7: v = base; break;                // AT_BASE
        case 9: v = exe.entry; break;           // AT_ENTRY
        case 31: v = (ul)argv0; break;          // AT_EXECFN
        }
        *w++ = t; *w++ = v;
    }
    *w++ = 0; *w++ = 0;
    enter_guest((void *)entry, top);
}
