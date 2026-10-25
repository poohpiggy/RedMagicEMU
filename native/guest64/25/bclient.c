// The binder front end inside a guest process: turns /dev/binder ioctls into messages for binderd64 (bproto.h).
// Included by run64.c; runs inside the SIGSYS handler, so no libc and little stack.

#include "bproto.h"

// syscalls defined in run64.c: NR_pwrite64, NR_sched_yield, NR_getpid, NR_gettid, NR_sendmsg, NR_recvmsg

#define BINDER_WRITE_READ 0xc0306201u
#define BINDER_SET_MAX_THREADS 0x40046205u
#define BINDER_SET_CONTEXT_MGR 0x40046207u
#define BINDER_THREAD_EXIT 0x40046208u
#define BINDER_VERSION 0xc0046209u
#define BC_TRANSACTION 0x40406300u
#define BC_REPLY 0x40406301u
#define BC_TRANSACTION_SG 0x40486311u
#define BC_REPLY_SG 0x40486312u
#define BC_FREE_BUFFER 0x40086303u
#define BC_AEMU_MAX_THREADS 0x4004633du
#define BC_AEMU_CTX_MGR 0x0000633eu
#define BR_NOOP 0x0000720cu
#define T_FD 0x66642a85u
#define T_FDA 0x66646185u
#define T_PTR 0x70742a85u
#define EAGAIN 11
#define EINTR 4
#define EBADF 9

struct bwr { u64 write_size, write_consumed, write_buffer, read_size, read_consumed, read_buffer; };
struct btd { u64 target, cookie; u32 code, flags; int sender_pid; u32 sender_euid; u64 data_size, offsets_size, buffer, offsets; };
// hwbinder (HIDL) objects: a buffer the driver copies to the other side and re-points, and descriptors kept in one
struct bbo { u32 type, flags; u64 buffer, length, parent, parent_offset; };
struct bfda { u32 type, pad; u64 num_fds, parent, parent_offset; };
#define A8(x) (((x) + 7) & ~7UL)
struct iov { void *base; u64 len; };
struct msgh { void *name; u32 namelen; struct iov *iov; u64 iovlen; void *control; u64 controllen; int flags; };
struct cmsg { u64 len; int level, type; };

static char g_bsock[108];                       // the daemon's socket (AEMU64_BINDER)
static char g_hwbsock[108];                     // AEMU64_HWBINDER
static char g_vndbsock[108];                    // AEMU64_VNDBINDER
static int g_bpid;                              // the process the state below belongs to
static int g_bfds[8];                           // descriptors open("/dev/binder", etc.) returned, +1
static char g_bfd_socks[8][108];
#define NTH 509
static struct bth { int tid, bfd, sock; char *scratch; } g_bth[NTH];
#define SCRATCH (INLINE_MAX + 8192)

// buffers of delivered transactions live in the area the guest mapped from the binder descriptor
struct bhdr { u64 size, id; u32 used, big; u64 pad; };
static u64 g_bmap, g_bmap_size;
static volatile int g_block;

static void block(void) { while (__atomic_exchange_n(&g_block, 1, __ATOMIC_ACQUIRE)) S0(NR_sched_yield); }
static void bunlock(void) { __atomic_store_n(&g_block, 0, __ATOMIC_RELEASE); }

static void *balloc(u64 n, u64 id) {
    n = (n + 15) & ~15UL;
    block();
    if (g_bmap) {
        u64 end = g_bmap + g_bmap_size;
        for (struct bhdr *h = (struct bhdr *)g_bmap; (u64)h < end;) {
            struct bhdr *nx = (struct bhdr *)((u64)(h + 1) + h->size);
            if (!h->used) {
                while ((u64)nx < end && !nx->used) { h->size += sizeof *nx + nx->size; nx = (struct bhdr *)((u64)(h + 1) + h->size); }
                if (h->size >= n) {
                    if (h->size >= n + sizeof *h + 64) {
                        struct bhdr *r = (struct bhdr *)((u64)(h + 1) + n);
                        r->size = h->size - n - sizeof *h; r->used = 0; r->big = 0;
                        h->size = n;
                    }
                    h->used = 1; h->big = 0; h->id = id;
                    bunlock();
                    return h + 1;
                }
            }
            h = nx;
        }
    }
    bunlock();
    u64 total = (n + sizeof(struct bhdr) + 4095) & ~4095UL;
    struct bhdr *h = (struct bhdr *)S6(NR_mmap, 0, total, 3, 0x22, -1, 0);
    if ((u64)h >= (u64)-4095) return 0;
    h->size = total; h->used = 1; h->big = 1; h->id = id;
    return h + 1;
}

static u64 bfree(u64 ptr) {
    struct bhdr *h = (struct bhdr *)ptr - 1;
    u64 id = h->id;
    if (h->big) S2(NR_munmap, h, h->size);
    else { block(); h->used = 0; bunlock(); }
    return id;
}

static int is_binder_fd(int fd) {
    for (int i = 0; i < 8; i++) if (g_bfds[i] == fd + 1) return 1;
    return 0;
}

static const char *sock_for_fd(int fd) {
    for (int i = 0; i < 8; i++) {
        if (g_bfds[i] == fd + 1 && g_bfd_socks[i][0]) return g_bfd_socks[i];
    }
    return g_bsock;
}

static long bconnect(int main_, int cloexec, int bfd, const char *sock_path) {
    long s = S3(NR_socket, 1, 5 /*SOCK_SEQPACKET*/ | (cloexec ? 02000000 : 0), 0);
    if (s < 0) return s;
    struct sockaddr_un sa; sa.family = 1;
    if (!sock_path || !sock_path[0]) sock_path = g_bsock;
    u64 l = slen(sock_path); memcpy(sa.path, sock_path, l + 1);
    long r = S3(NR_connect, s, &sa, 2 + l + 1);
    if (r < 0) { S1(NR_close, s); return r; }
    struct c_hello m = { { OP_HELLO, 0, 0, (u32)g_uid }, (u32)S0(NR_getpid), (u32)S0(NR_gettid), (u32)main_, (u32)(main_ ? s : bfd) };
    struct iov v = { &m, sizeof m }; struct msgh mh = { 0, 0, &v, 1, 0, 0, 0 };
    S3(NR_sendmsg, s, &mh, 0x4000 /*MSG_NOSIGNAL*/);
    return s;
}

static struct bth *bthread(int bfd, int create) {
    int tid = (int)S0(NR_gettid);
    u32 i = (u32)tid % NTH, j = i;
    for (int n = 0; n < NTH; n++, i = (i + 1) % NTH) {
        struct bth *t = &g_bth[i];
        if (t->tid == tid && t->bfd == bfd) return t;
        if (t->tid == 0) break;
    }
    if (!create) return 0;
    for (int n = 0; n < NTH; n++, j = (j + 1) % NTH) {      // a free slot, or one left by a thread that exited
        struct bth *t = &g_bth[j];
        int z = t->tid;
        if ((z == 0 || z == -1) && __atomic_compare_exchange_n(&t->tid, &z, tid, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            t->bfd = bfd; t->sock = 0;
            return t;
        }
    }
    return 0;
}

static long binder_open(long flags, const char *dev_name) {
    const char *target_sock = g_bsock;
    if (dev_name) {
        if (seq(dev_name, "/dev/hwbinder") && g_hwbsock[0]) target_sock = g_hwbsock;
        else if (seq(dev_name, "/dev/vndbinder") && g_vndbsock[0]) target_sock = g_vndbsock;
    }
    if (!target_sock[0]) return -ENOENT;
    int pid = (int)S0(NR_getpid);
    if (g_bpid != pid) {                        // a fresh process (or a fork child): nothing is inherited
        memset(g_bth, 0, sizeof g_bth); memset(g_bfds, 0, sizeof g_bfds);
        memset(g_bfd_socks, 0, sizeof g_bfd_socks);
        g_bmap = 0; g_block = 0; g_bpid = pid;
    }
    long s = bconnect(1, (flags & 02000000) != 0, -1, target_sock);
    if (s < 0) return s;
    for (int i = 0; i < 8; i++) if (!g_bfds[i]) {
        g_bfds[i] = (int)s + 1;
        u64 l = slen(target_sock); if (l < sizeof g_bfd_socks[i]) memcpy(g_bfd_socks[i], target_sock, l + 1);
        break;
    }
    struct bth *t = bthread((int)s, 1);
    if (t) t->sock = (int)s;
    return s;
}

static long binder_mmap(u64 addr, u64 len, long flags) {
    // qemu-arm maps at an address of its choice (MAP_FIXED inside the guest's 4 GB): keep it
    u64 m = (u64)S6(NR_mmap, addr, len, 3, 0x22 | (flags & 0x10), -1, 0);
    if (m >= (u64)-4095) return (long)m;
    block();
    struct bhdr *h = (struct bhdr *)m; h->size = len - sizeof *h; h->used = 0; h->big = 0;
    g_bmap = m; g_bmap_size = len;
    bunlock();
    return (long)m;
}

static long bsendv(int sock, struct iov *v, int n, int *fds, int nfds);

static long bsend(int sock, void *a, u64 an, void *b, u64 bn, void *c, u64 cn, int *fds, int nfds) {
    struct iov v[4]; int n = 0; static const u64 zero;
    v[n].base = a; v[n++].len = an;
    if (bn) { v[n].base = b; v[n++].len = bn; if (cn && (bn & 7)) { v[n].base = (void *)&zero; v[n++].len = 8 - (bn & 7); } }
    if (cn) { v[n].base = c; v[n++].len = cn; }
    return bsendv(sock, v, n, fds, nfds);
}

static long bsendv(int sock, struct iov *v, int n, int *fds, int nfds) {
    struct { struct cmsg h; int fd[254]; } cm;
    struct msgh mh = { 0, 0, v, (u64)n, 0, 0, 0 };
    if (nfds) {
        cm.h.len = sizeof cm.h + (u64)nfds * 4; cm.h.level = 1; cm.h.type = 1;
        memcpy(cm.fd, fds, (u64)nfds * 4);
        mh.control = &cm; mh.controllen = (sizeof cm.h + (u64)nfds * 4 + 7) & ~7UL;
    }
    long r;
    do r = S3(NR_sendmsg, sock, &mh, 0x4000); while (r == -EINTR);
    return r;
}

static char *bscratch(struct bth *t) {
    if (!t->scratch) {
        u64 m = (u64)S6(NR_mmap, 0, SCRATCH, 3, 0x22, -1, 0);
        if (m >= (u64)-4095) return 0;
        t->scratch = (char *)m;
    }
    return t->scratch;
}

static long btxn(struct bth *t, u32 cmd, struct btd *d, u32 flags, u32 read_size) {
    struct c_txn m = { { OP_TXN, flags, read_size, (u32)g_uid }, cmd, (u32)d->target, d->code, d->flags, d->data_size, d->offsets_size, 0 };
    int fds[254], nfds = 0;
    u64 *offs = (u64 *)d->offsets, no = d->offsets_size / 8, extra = 0;
    for (u64 i = 0; i < no; i++) {
        if (offs[i] + 24 > d->data_size) continue;
        u32 *o = (u32 *)(d->buffer + offs[i]);
        if (o[0] == T_FD && nfds < 252) fds[1 + nfds++] = (int)o[2];
        else if (o[0] == T_PTR) extra += A8(((struct bbo *)o)->length);
        else if (o[0] == T_FDA) {
            struct bfda *f = (struct bfda *)o;
            if (f->parent >= i) return -EINVAL;
            struct bbo *par = (struct bbo *)(d->buffer + offs[f->parent]);
            int *src = (int *)(par->buffer + f->parent_offset);
            if (par->type != T_PTR || f->num_fds > (u64)(252 - nfds)) return -EINVAL;
            for (u64 k = 0; k < f->num_fds; k++) fds[1 + nfds++] = src[k];
        }
    }
    m.extra_size = extra;
    u64 dsz = A8(d->data_size), total = dsz + d->offsets_size + extra;
    if (total > INLINE_MAX) {
        long mf = S2(NR_memfd_create, "aemu-binder", 1);
        if (mf < 0) return mf;
        S4(NR_pwrite64, mf, d->buffer, d->data_size, 0);
        S4(NR_pwrite64, mf, d->offsets, d->offsets_size, dsz);
        u64 at = dsz + d->offsets_size;
        if (extra) for (u64 i = 0; i < no; i++) {
            struct bbo *o = (struct bbo *)(d->buffer + offs[i]);
            if (offs[i] + 40 > d->data_size || o->type != T_PTR) continue;
            if (o->length) S4(NR_pwrite64, mf, o->buffer, o->length, at);
            at += A8(o->length);
        }
        m.h.flags |= F_BIG; fds[0] = (int)mf;
        long r = bsend(t->sock, &m, sizeof m, 0, 0, 0, 0, fds, nfds + 1);
        S1(NR_close, mf);
        return r;
    }
    if (!extra) return bsend(t->sock, &m, sizeof m, (void *)d->buffer, d->data_size, (void *)d->offsets, d->offsets_size, fds + 1, nfds);
    static const u64 zero;
    char *x = bscratch(t);
    if (!x) return -ENOMEM;
    u64 at = 0;
    for (u64 i = 0; i < no; i++) {
        struct bbo *o = (struct bbo *)(d->buffer + offs[i]);
        if (offs[i] + 40 > d->data_size || o->type != T_PTR) continue;
        if (o->length) memcpy(x + at, (void *)o->buffer, o->length);
        for (u64 k = o->length; k < A8(o->length); k++) x[at + k] = 0;
        at += A8(o->length);
    }
    struct iov v[5]; int n = 0;
    v[n].base = &m; v[n++].len = sizeof m;
    v[n].base = (void *)d->buffer; v[n++].len = d->data_size;
    if (d->data_size & 7) { v[n].base = (void *)&zero; v[n++].len = 8 - (d->data_size & 7); }
    v[n].base = (void *)d->offsets; v[n++].len = d->offsets_size;
    v[n].base = x; v[n++].len = extra;
    return bsendv(t->sock, v, n, fds + 1, nfds);
}

static long bread(struct bth *t, struct bwr *w) {
    if (!bscratch(t)) return -ENOMEM;
    struct d_hdr h;
    struct { struct cmsg h; int fd[254]; } cm;
    struct iov v[2] = { { &h, sizeof h }, { t->scratch, SCRATCH } };
    struct msgh mh = { 0, 0, v, 2, &cm, sizeof cm, 0 };
    long r;
    do {
        mh.controllen = sizeof cm;
        r = S3(NR_recvmsg, t->sock, &mh, 0x40000000 /*MSG_CMSG_CLOEXEC*/);
    } while (r == -EINTR || (r >= (long)sizeof h && (h.flags & F_WAKE)));
    if (r < (long)sizeof h) return r < 0 ? r : -EBADF;
    if (h.flags & F_AGAIN) return -EAGAIN;
    int nfds = 0, *fds = cm.fd;
    if (mh.controllen >= sizeof cm.h && cm.h.level == 1 && cm.h.type == 1) nfds = (int)((cm.h.len - sizeof cm.h) / 4);
    u32 *out = (u32 *)(w->read_buffer + w->read_consumed);
    *out++ = BR_NOOP;
    memcpy(out, t->scratch, h.br_size); out = (u32 *)((char *)out + h.br_size);
    if (h.flags & F_TXN) {
        u64 dsz = A8(h.data_size), total = dsz + h.offsets_size + h.extra_size;
        char *buf = balloc(total ? total : 8, h.id);
        if (!buf) return -ENOMEM;
        if (h.flags & F_BIG) {
            u64 got = 0;
            while (got < total) { long k = S4(NR_pread64, fds[0], buf + got, total - got, got); if (k <= 0) break; got += (u64)k; }
            S1(NR_close, fds[0]); fds++; nfds--;
        } else memcpy(buf, t->scratch + h.br_size, total);
        u64 *offs = (u64 *)(buf + dsz), no = h.offsets_size / 8; int k = 0;
        char *sg = (char *)(offs + no), *sgend = buf + total;
        for (u64 i = 0; i < no; i++) {
            u32 *o = (u32 *)(buf + offs[i]);
            if (o[0] == T_FD) { o[2] = k < nfds ? (u32)fds[k] : (u32)-1; o[3] = 0; k++; }
            else if (o[0] == T_PTR) {           // the buffer is here now; the pointer to it inside its parent follows
                struct bbo *b = (struct bbo *)o;
                if (A8(b->length) > (u64)(sgend - sg)) { b->buffer = 0; b->length = 0; continue; }
                b->buffer = (u64)sg; sg += A8(b->length);
                if ((b->flags & 1) && b->parent < i) {
                    struct bbo *par = (struct bbo *)(buf + offs[b->parent]);
                    if (par->type == T_PTR && b->parent_offset + 8 <= par->length) *(u64 *)(par->buffer + b->parent_offset) = b->buffer;
                }
            } else if (o[0] == T_FDA) {
                struct bfda *f = (struct bfda *)o;
                if (f->parent >= i) continue;
                struct bbo *par = (struct bbo *)(buf + offs[f->parent]);
                if (par->type != T_PTR || f->parent_offset + f->num_fds * 4 > par->length) continue;
                int *dst = (int *)(par->buffer + f->parent_offset);
                for (u64 j = 0; j < f->num_fds; j++, k++) dst[j] = k < nfds ? fds[k] : -1;
            }
        }
        struct btd d = { h.ptr, h.cookie, h.code, h.tflags, h.sender_pid, h.sender_euid, h.data_size, h.offsets_size, (u64)buf, (u64)offs };
        *out++ = h.cmd; memcpy(out, &d, sizeof d); out = (u32 *)((char *)out + sizeof d);
    } else for (int i = 0; i < nfds; i++) S1(NR_close, fds[i]);
    w->read_consumed = (u64)((char *)out - (char *)w->read_buffer);
    return 0;
}

static long bwrite_read(struct bth *t, struct bwr *w) {
    u32 small[256]; u32 ns = 0;
    struct c_hdr sh = { OP_BCS, 0, (u32)w->read_size, (u32)g_uid };
    int want = w->read_size > w->read_consumed, asked = 0;
    char *p = (char *)w->write_buffer + w->write_consumed, *end = (char *)w->write_buffer + w->write_size;
    long r = 0;
    while (p + 4 <= end) {
        u32 cmd = *(u32 *)p, sz = (cmd >> 16) & 0x3fff;
        if (p + 4 + sz > end) break;
        if (cmd == BC_TRANSACTION_SG || cmd == BC_REPLY_SG) cmd = cmd == BC_REPLY_SG ? BC_REPLY : BC_TRANSACTION;   // the buffers are found by their objects
        if (cmd == BC_TRANSACTION || cmd == BC_REPLY) {
            if (ns) { if ((r = bsend(t->sock, &sh, sizeof sh, small, ns * 4, 0, 0, 0, 0)) < 0) return r; ns = 0; }
            int last = p + 4 + sz >= end && want;
            if ((r = btxn(t, cmd, (struct btd *)(p + 4), last ? F_READ : 0, (u32)w->read_size)) < 0) return r;
            asked = last;
        } else {
            if (ns + 8 > 256) { if ((r = bsend(t->sock, &sh, sizeof sh, small, ns * 4, 0, 0, 0, 0)) < 0) return r; ns = 0; }
            if (cmd == BC_FREE_BUFFER) {
                u64 ptr; memcpy(&ptr, p + 4, 8);
                u64 id = bfree(ptr);
                small[ns++] = BC_AEMU_FREE; memcpy(small + ns, &id, 8); ns += 2;
            } else if (sz <= 16) {
                small[ns++] = cmd; memcpy(small + ns, p + 4, sz); ns += (sz + 3) / 4;
            }
            asked = 0;
        }
        p += 4 + sz;
    }
    w->write_consumed = w->write_size;
    if (ns) {
        if (want) sh.flags = F_READ;
        if ((r = bsend(t->sock, &sh, sizeof sh, small, ns * 4, 0, 0, 0, 0)) < 0) return r;
        asked = want;
    }
    if (!want) return 0;
    if (!asked) {
        struct c_hdr rh = { OP_READ, F_READ, (u32)w->read_size, (u32)g_uid };
        if ((r = bsend(t->sock, &rh, sizeof rh, 0, 0, 0, 0, 0, 0)) < 0) return r;
    }
    return bread(t, w);
}

// ------------------------------------------------------------------ the old 32-bit protocol (BINDER_IPC_32BIT)
// A whole 32-bit firmware (its libbinder built for a 32-bit kernel) speaks binder version 7: pointers, cookies and
// sizes are 32-bit, so flat objects are 16 bytes instead of 24. Its programs run under qemu-arm (or aemu32) with
// guest addresses equal to ours; the stream is widened to the version 8 one binderd64 knows, and narrowed back on
// the way in. Every peer of such a firmware is 32-bit, so the parcels come out exactly as they went in.
typedef unsigned long ul;
static int g_b32;                               // AEMU64_B32: the firmware's binder is the 32-bit one
// qemu-arm's guest_base for a static program linked below the lowest address the phone maps (healthd at 0x8000):
// the guest's pointers are this much below ours
static u32 g_gbase;
#define G(x) ((ul)(x) + g_gbase)

#define BINDER_WRITE_READ32 0xc0186201u
struct bwr32 { u32 write_size, write_consumed, write_buffer, read_size, read_consumed, read_buffer; };
struct btd32 { u32 target, cookie, code, flags; int sender_pid; u32 sender_euid; u32 data_size, offsets_size, buffer, offsets; };

static int bobj(u32 type) {                     // a flat_binder_object: binder, weak binder, handle, weak handle, fd
    return type == 0x73622a85u || type == 0x77622a85u || type == 0x73682a85u || type == 0x77682a85u || type == T_FD;
}

static void bmove(char *d, const char *s, u64 n) { while (n--) *d++ = *s++; }   // forward, overlap allowed when d <= s

// 32-bit parcel -> 64-bit parcel at out: each object grows by 8 bytes; returns the new data size, offsets after it
static u64 bwiden(const struct btd32 *d, char *out, u64 *offs) {
    const char *src = (const char *)G(d->buffer); const u32 *o32 = (const u32 *)G(d->offsets);
    u64 s = 0, at = 0, n = d->offsets_size / 4;
    for (u64 i = 0; i < n; i++) {
        u64 o = o32[i];
        if (o < s || o + 16 > d->data_size || !bobj(*(const u32 *)(src + o))) { offs[i] = at + (o - s); continue; }   // left as it is
        memcpy(out + at, src + s, o - s); at += o - s;
        const u32 *b = (const u32 *)(src + o); u32 *w = (u32 *)(out + at);
        w[0] = b[0]; w[1] = b[1]; w[2] = b[2]; w[3] = 0; w[4] = b[3]; w[5] = 0;
        offs[i] = at; at += 24; s = o + 16;
    }
    memcpy(out + at, src + s, d->data_size - s);
    return at + d->data_size - s;
}

// a delivered 64-bit parcel, narrowed in place; the offsets become 32-bit words at the same place
static void bnarrow(char *buf, u64 *size, u64 *offs, u64 n) {
    u64 s = 0, at = 0;
    for (u64 i = 0; i < n; i++) {
        u64 o = offs[i];
        if (o < s || o + 24 > *size || !bobj(*(u32 *)(buf + o))) { ((u32 *)offs)[i] = (u32)(at + (o - s)); continue; }
        bmove(buf + at, buf + s, o - s); at += o - s;
        u32 *b = (u32 *)(buf + o), v[4] = { b[0], b[1], b[2], b[4] };
        memcpy(buf + at, v, 16);
        ((u32 *)offs)[i] = (u32)at; at += 16; s = o + 24;
    }
    bmove(buf + at, buf + s, *size - s);
    *size = at + *size - s;
}

static long bwrite_read32(struct bth *t, struct bwr32 *w) {
    // the 64-bit stream: small commands in place, a transaction's widened parcel in its own mapping
    u32 cmds[512]; u32 nc = 0;
    char *p = (char *)G(w->write_buffer) + w->write_consumed, *end = (char *)G(w->write_buffer) + w->write_size;
    long r = 0;
    char *big = 0; u64 bigsz = 0;
    struct btd d64;
    while (p + 4 <= end && nc + 20 < 512) {
        u32 cmd = *(u32 *)p, sz = (cmd >> 16) & 0x3fff, *a = (u32 *)(p + 4);
        if (p + 4 + sz > end) break;
        switch (cmd) {
        case 0x40286300u: case 0x40286301u: {   // BC_TRANSACTION, BC_REPLY: one per write is what libbinder sends
            const struct btd32 *d = (const struct btd32 *)a;
            u64 n = d->offsets_size / 4, need = A8(d->data_size + 8 * n) + 8 * n + 16;
            if (big) S2(NR_munmap, big, bigsz);
            bigsz = (need + 4095) & ~4095UL;
            big = (char *)S6(NR_mmap, 0, bigsz, 3, 0x22, -1, 0);
            if ((ul)big >= (ul)-4095) return -ENOMEM;
            u64 *offs = (u64 *)(big + A8(d->data_size + 8 * n));
            u64 dsz = bwiden(d, big, offs);
            d64 = (struct btd){ d->target, d->cookie, d->code, d->flags, 0, 0, dsz, n * 8, (u64)(ul)big, (u64)(ul)offs };
            cmds[nc++] = cmd == 0x40286300u ? BC_TRANSACTION : BC_REPLY;
            memcpy(cmds + nc, &d64, sizeof d64); nc += sizeof d64 / 4;
            break;
        }
        case 0x40046303u:                       // BC_FREE_BUFFER (u32 ptr)
            cmds[nc++] = BC_FREE_BUFFER; cmds[nc++] = (u32)G(a[0]); cmds[nc++] = 0; break;
        case 0x40086308u: case 0x40086309u:     // BC_INCREFS_DONE, BC_ACQUIRE_DONE: ptr, cookie
            cmds[nc++] = cmd + 0x80000u; cmds[nc++] = a[0]; cmds[nc++] = 0; cmds[nc++] = a[1]; cmds[nc++] = 0; break;
        case 0x4008630eu: case 0x4008630fu:     // BC_REQUEST/CLEAR_DEATH_NOTIFICATION: handle, cookie
            cmds[nc++] = cmd + 0x40000u; cmds[nc++] = a[0]; cmds[nc++] = a[1]; cmds[nc++] = 0; break;
        case 0x40046310u:                       // BC_DEAD_BINDER_DONE: cookie
            cmds[nc++] = 0x40086310u; cmds[nc++] = a[0]; cmds[nc++] = 0; break;
        default:                                // handles and plain commands are the same in both
            if (sz <= 16) { cmds[nc++] = cmd; memcpy(cmds + nc, a, sz); nc += (sz + 3) / 4; }
        }
        p += 4 + sz;
    }
    w->write_consumed = (u32)(p - (char *)G(w->write_buffer));
    // the answer arrives 64-bit in a buffer of ours, at most twice the size it will have
    u64 rs = w->read_size > w->read_consumed ? w->read_size - w->read_consumed : 0;
    char *rb = 0; u64 rbsz = 0;
    if (rs) {
        rbsz = (2 * rs + 64 + 4095) & ~4095UL;
        rb = (char *)S6(NR_mmap, 0, rbsz, 3, 0x22, -1, 0);
        if ((ul)rb >= (ul)-4095) { r = -ENOMEM; goto out; }
    }
    struct bwr w64 = { (u64)nc * 4, 0, (u64)(ul)cmds, rs ? 2 * rs : 0, 0, (u64)(ul)rb };
    r = nc || rs ? bwrite_read(t, &w64) : 0;
    if (r < 0 || !rs) goto out;
    // 64-bit BR_* -> 32-bit ones
    char *q = rb, *qe = rb + w64.read_consumed, *o = (char *)G(w->read_buffer) + w->read_consumed, *oe = o + rs;
    while (q + 4 <= qe && o + 4 <= oe) {
        u32 cmd = *(u32 *)q, sz = (cmd >> 16) & 0x3fff, *a = (u32 *)(q + 4), *b = (u32 *)(o + 4);
        if (cmd == 0x80407202u || cmd == 0x80407203u) {         // BR_TRANSACTION, BR_REPLY
            struct btd *d = (struct btd *)a;
            if (o + 44 > oe) break;
            u64 dsz = d->data_size, n = d->offsets_size / 8;
            if (d->buffer) bnarrow((char *)(ul)d->buffer, &dsz, (u64 *)(ul)d->offsets, n);
            struct btd32 e = { (u32)d->target, (u32)d->cookie, d->code, d->flags, d->sender_pid, d->sender_euid,
                               (u32)dsz, (u32)(n * 4), (u32)(d->buffer - g_gbase), (u32)(d->offsets - g_gbase) };
            *(u32 *)o = cmd == 0x80407202u ? 0x80287202u : 0x80287203u; memcpy(b, &e, sizeof e); o += 44;
        } else if (cmd >= 0x80107207u && cmd <= 0x8010720au) {   // BR_INCREFS, ACQUIRE, RELEASE, DECREFS
            if (o + 12 > oe) break;
            *(u32 *)o = cmd - 0x80000u; b[0] = a[0]; b[1] = a[2]; o += 12;
        } else if (cmd == 0x8008720fu || cmd == 0x80087210u) {   // BR_DEAD_BINDER, BR_CLEAR_DEATH_NOTIFICATION_DONE
            if (o + 8 > oe) break;
            *(u32 *)o = cmd - 0x40000u; b[0] = a[0]; o += 8;
        } else {
            if (o + 4 + sz > oe) break;
            memcpy(o, q, 4 + sz); o += 4 + sz;
        }
        q += 4 + sz;
    }
    w->read_consumed = (u32)(o - (char *)G(w->read_buffer));
out:
    if (rb) S2(NR_munmap, rb, rbsz);
    if (big) S2(NR_munmap, big, bigsz);
    return r < 0 ? r : 0;
}

static long binder_ioctl(int fd, u32 cmd, u64 arg) {
    if (g_is32 && arg) arg = G(arg);
    struct bth *t = bthread(fd, 1);
    if (!t) return -ENOMEM;
    if (!t->sock) {
        long s = bconnect(0, 1, fd, sock_for_fd(fd));
        if (s < 0) { t->tid = 0; return s; }
        t->sock = (int)s;
    }
    switch (cmd) {
    case BINDER_WRITE_READ: return bwrite_read(t, (struct bwr *)arg);
    case BINDER_WRITE_READ32: return bwrite_read32(t, (struct bwr32 *)arg);
    case BINDER_VERSION: *(int *)arg = g_b32 ? 7 : 8; return 0;
    case BINDER_SET_MAX_THREADS: case BINDER_SET_CONTEXT_MGR: {
        struct { struct c_hdr h; u32 c, v; } m = { { OP_BCS, 0, 0, (u32)g_uid }, cmd == BINDER_SET_MAX_THREADS ? BC_AEMU_MAX_THREADS : BC_AEMU_CTX_MGR, arg ? *(u32 *)arg : 0 };
        return bsend(t->sock, &m, cmd == BINDER_SET_MAX_THREADS ? sizeof m : sizeof m - 4, 0, 0, 0, 0, 0, 0) < 0 ? -EBADF : 0;
    }
    case BINDER_THREAD_EXIT:
        if (t->sock != fd) S1(NR_close, t->sock);
        t->sock = 0; t->bfd = -1; t->tid = -1;      // the slot stays taken: probing must not stop here
        return 0;
    }
    return -EINVAL;
}
