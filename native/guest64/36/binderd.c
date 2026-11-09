// binderd64 — the binder driver for natively running guests, in user space.
//
//   binderd64 <socket path>
//
// Every guest thread talks to it over its own SOCK_SEQPACKET connection (see bproto.h); the daemon keeps what the
// kernel driver keeps: nodes, references, transaction stacks, death notifications, looper bookkeeping. It is
// single-threaded: one epoll loop, no locks. A process dies when its main connection closes.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "bproto.h"
#include "display_trace.h"
#include "binder_txn_trace.h"
#include "binder_hw_get_trace.h"
#include "binder_pid_guard.h"

typedef uint64_t u64;
typedef uint32_t u32;

#define BC_TRANSACTION 0x40406300u
#define BC_REPLY 0x40406301u
#define BC_FREE_BUFFER 0x40086303u
#define BC_INCREFS 0x40046304u
#define BC_ACQUIRE 0x40046305u
#define BC_RELEASE 0x40046306u
#define BC_DECREFS 0x40046307u
#define BC_INCREFS_DONE 0x40106308u
#define BC_ACQUIRE_DONE 0x40106309u
#define BC_REGISTER_LOOPER 0x0000630bu
#define BC_ENTER_LOOPER 0x0000630cu
#define BC_EXIT_LOOPER 0x0000630du
#define BC_REQUEST_DEATH_NOTIFICATION 0x400c630eu
#define BC_CLEAR_DEATH_NOTIFICATION 0x400c630fu
#define BC_DEAD_BINDER_DONE 0x40086310u
/* private: ioctls the front end forwards */
#define BC_AEMU_MAX_THREADS 0x4004633du
#define BC_AEMU_CTX_MGR 0x0000633eu

#define BR_TRANSACTION 0x80407202u
#define BR_REPLY 0x80407203u
#define BR_DEAD_REPLY 0x00007205u
#define BR_TRANSACTION_COMPLETE 0x00007206u
#define BR_INCREFS 0x80107207u
#define BR_ACQUIRE 0x80107208u
#define BR_RELEASE 0x80107209u
#define BR_DECREFS 0x8010720au
#define BR_SPAWN_LOOPER 0x0000720du
#define BR_DEAD_BINDER 0x8008720fu
#define BR_CLEAR_DEATH_NOTIFICATION_DONE 0x80087210u
#define BR_FAILED_REPLY 0x00007211u

#define T_BINDER 0x73622a85u
#define T_WEAK_BINDER 0x77622a85u
#define T_HANDLE 0x73682a85u
#define T_WEAK_HANDLE 0x77682a85u
#define T_FD 0x66642a85u
#define T_FDA 0x66646185u
#define T_PTR 0x70742a85u
#define TF_ONE_WAY 1

struct fbo { u32 type, flags; u64 binder, cookie; };   /* flat_binder_object, 64-bit; handle = low word of binder */

struct proc; struct thread; struct node; struct txn;

enum { W_TXN = 1, W_COMPLETE, W_NODE, W_DEAD_BINDER, W_CLEAR_DONE, W_ERROR };
struct work { struct work *next; int type, deferred; struct txn *t; struct node *node; u64 cookie; u32 err; };
struct wq { struct work *head, *tail; };

struct ref { struct proc *proc; u32 desc; struct node *node; int strong, weak, has_death; u64 death; struct ref *next_in_node; };
struct node {
    struct proc *owner; u64 ptr, cookie; int internal_strong, local_strong, local_weak, nrefs, has_strong, has_weak, queued, has_async;
    int pend_strong, pend_weak;     /* BR_ACQUIRE / BR_INCREFS sent, not yet confirmed: the node keeps a reference of its own until then */
    struct ref *refs; struct wq async; struct node *hnext;
};
struct held { struct ref *ref; struct node *node; int strong; };      /* what a transaction buffer keeps alive */
struct dbuf { struct dbuf *prev, *next; struct proc *proc; struct node *async_node; int nheld; struct held *held; };
struct txn {
    struct thread *from, *to_thread; struct proc *to_proc; struct txn *from_parent, *to_parent;
    struct node *target; u32 code, flags; int reply, need_reply, sender_pid; u32 sender_euid;
    u64 data_size, offsets_size, extra_size; char *data; int memfd; int nfds, *fds; int nheld; struct held *held;
    int display_trace_code;
    unsigned binder_txn_trace_id;
    unsigned media_txn_trace_id;
};
struct thread {
    int fd, tid, is_main, waiting, nonblock, looper, woken; u32 read_size;
    int pid_guard_enabled; struct binder_pid_guard pid_guard;
    struct proc *proc; struct wq todo; struct txn *stack; struct thread *next;
};
#define NHASH 256
struct proc {
    int pid, max_threads, requested, started, kick; u32 key; struct thread *threads; struct wq todo;
    struct ref **refs; u32 nrefs; struct node *nodes[NHASH]; struct dbuf *bufs; struct proc *next;
};

static struct proc *procs;
static struct node *ctx_mgr;
/* the thread whose message is being handled: reference changes of its own objects are reported to it first,
 * before it can drop the object it has just sent (the driver does the same) */
static struct thread *cur;
static int kick_any;
static int ep, verbose, display_trace_enabled;
/* Experimental, off by default. It authenticates physical PID only. */
static int binder_strict_pid;
static unsigned display_trace_phases;
static struct display_trace_tracker display_trace_tracker;
static int binder_txn_trace_enabled;
static struct btt_state binder_txn_trace_state;
static int media_txn_trace_enabled, media_txn_trace_driver;
static struct btt_state media_txn_trace_state;
static int strict_snapshot_memfd(int input, u64 data_size, u64 offsets_size);

static void logf_(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

/* Identify only the already-observed process and thread; never log their argv. */
static int binder_txn_trace_match(int pid, int tid) {
    char path[96], exe[1024], cmd[1024], comm[32];
    if (pid <= 0 || tid <= 0) return 0;
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    ssize_t n = readlink(path, exe, sizeof exe - 1);
    if (n <= 0 || n >= (ssize_t)sizeof exe - 1) return 0;
    exe[n] = 0;
    snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    n = read(fd, cmd, sizeof cmd); close(fd);
    if (n <= 0 || !btt_identity(cmd, (unsigned long)n, exe)) return 0;
    snprintf(path, sizeof path, "/proc/%d/task/%d/comm", pid, tid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    n = read(fd, comm, sizeof comm); close(fd);
    return n > 0 && btt_thread(comm, (unsigned long)n);
}

/* This identity is intentionally limited to the API 36 guest mediaserver. */
static int media_txn_process_match(int pid, int hwmanager) {
    char path[96], exe[1024], cmd[1024];
    if (pid <= 0) return 0;
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    ssize_t n = readlink(path, exe, sizeof exe - 1);
    if (n <= 0 || n >= (ssize_t)sizeof exe - 1) return 0;
    exe[n] = 0;
    static const char suffix[] = "/libaemu64_36.so";
    size_t len = (size_t)n;
    if (len < sizeof suffix - 1 || strcmp(exe + len - (sizeof suffix - 1), suffix)) return 0;
    snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    n = read(fd, cmd, sizeof cmd); close(fd);
    return n > 0 && (hwmanager ? bmt_hwmanager_identity(cmd, (unsigned long)n, exe) :
                                  bmt_identity(cmd, (unsigned long)n, exe));
}

static void media_txn_trace_event(unsigned id, const char *phase, int pid, int tid,
                                  unsigned handle, unsigned code, unsigned detail) {
    if (!bmt_event(&media_txn_trace_state, id)) return;
    logf_("media-binder-txn: driver=%s id=%u phase=%s pid=%d tid=%d handle=%u code=%u detail=%u",
          media_txn_trace_driver == 1 ? "main" : "hw", id, phase, pid, tid, handle, code, detail);
    if (media_txn_trace_state.events == BMT_LIMIT)
        logf_("media-binder-txn: driver=%s event limit reached; later transactions unobserved",
              media_txn_trace_driver == 1 ? "main" : "hw");
}

static void binder_txn_trace_event(unsigned id, const char *phase, int pid, int tid,
                                   unsigned handle, unsigned code, unsigned flags) {
    if (!btt_event(&binder_txn_trace_state, id)) return;
    logf_("binder-txn: id=%u phase=%s pid=%d tid=%d handle=%u code=%u detail=%u",
          id, phase, pid, tid, handle, code, flags);
    if (binder_txn_trace_state.events == 256)
        logf_("binder-txn: event limit reached; later transactions unobserved");
}
#define V(...) do { if (verbose) logf_(__VA_ARGS__); } while (0)

enum display_phase {
    PHASE_CANDIDATE, PHASE_UID_MATCH, PHASE_UID_OTHER, PHASE_BIG, PHASE_INLINE_INVALID,
    PHASE_INTERFACE_MATCH, PHASE_INTERFACE_OTHER,
    PHASE_SOURCE_MATCH, PHASE_SOURCE_COMM_UNAVAILABLE, PHASE_SOURCE_COMM_OTHER,
    PHASE_SOURCE_EXE_UNAVAILABLE, PHASE_SOURCE_EXE_OTHER,
    PHASE_TARGET_MATCH, PHASE_TARGET_COMM_UNAVAILABLE, PHASE_TARGET_COMM_OTHER,
    PHASE_TARGET_EXE_UNAVAILABLE, PHASE_TARGET_EXE_OTHER, PHASE_MATCH_LIMIT
};

static void display_trace_phase(enum display_phase phase) {
    static const char *const names[] = {
        "candidate", "uid-match", "uid-other", "big-unread", "inline-invalid",
        "interface-match", "interface-other", "source-match", "source-name-unavailable",
        "source-name-other", "source-exe-unavailable", "source-exe-other", "target-match",
        "target-name-unavailable", "target-name-other", "target-exe-unavailable", "target-exe-other", "transition-limit"
    };
    if (display_trace_enabled && (unsigned)phase < sizeof names / sizeof names[0] &&
        display_trace_once(&display_trace_phases, (unsigned)phase))
        logf_("display-trace: phase=%s", names[phase]);
}

static void display_trace_identity_phase(int target, enum display_trace_identity_result state) {
    unsigned index = (unsigned)state - (unsigned)DISPLAY_ID_MATCH;
    if (index < 5) display_trace_phase((enum display_phase)((target ? PHASE_TARGET_MATCH : PHASE_SOURCE_MATCH) + index));
}

/* The daemon handles many guests' threads. Never identify one by numeric UID alone. */
static enum display_trace_identity_result display_trace_process(int pid, int target) {
    char path[64], comm[64], exe[512], cmdline[512];
    const char *source_comm = NULL;
    if (pid <= 0 || snprintf(path, sizeof path, "/proc/%d/exe", pid) >= (int)sizeof path) return DISPLAY_ID_EXE_UNAVAILABLE;
    ssize_t n = readlink(path, exe, sizeof exe - 1);
    if (n <= 0 || n >= (ssize_t)sizeof exe - 1) return DISPLAY_ID_EXE_UNAVAILABLE;
    exe[n] = 0;
    if (!target && snprintf(path, sizeof path, "/proc/%d/comm", pid) < (int)sizeof path) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            n = read(fd, comm, sizeof comm - 1); close(fd);
            if (n > 0 && n < (ssize_t)sizeof comm - 1) {
                if (comm[n - 1] == '\n') n--;
                comm[n] = 0; source_comm = comm;
            }
        }
    }
    if (source_comm && !strcmp(source_comm, "system_server"))
        return display_trace_process_identity(source_comm, exe, NULL, 0, target);
    if (snprintf(path, sizeof path, "/proc/%d/cmdline", pid) >= (int)sizeof path)
        return display_trace_process_identity(source_comm, exe, NULL, 0, target);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return display_trace_process_identity(source_comm, exe, NULL, 0, target);
    n = read(fd, cmdline, sizeof cmdline); close(fd);
    return display_trace_process_identity(source_comm, exe, n > 0 ? cmdline : NULL,
                                           n > 0 ? (size_t)n : 0, target);
}

/* F_BIG is a shared, possibly truncated memfd; never touch it for diagnostics. */
static int display_trace_data_safe(const struct txn *t, u64 total, size_t received) {
    if (t->memfd >= 0 || t->data_size > UINT64_MAX - 7) return 0;
    u64 aligned = (t->data_size + 7) & ~7ULL;
    if (aligned > total || t->offsets_size > total - aligned) return 0;
    /* bsend adds alignment padding only when object offsets follow the data. */
    return (t->offsets_size ? total : t->data_size) <= received;
}

static int display_trace_decode(const struct txn *request, const struct txn *response,
                                const char *data, u64 total, size_t received,
                                struct display_trace_result *r) {
    u64 aligned;
    if (!display_trace_data_safe(response, total, received)) return 0;
    aligned = (response->data_size + 7) & ~7ULL;
    if (aligned > total || response->offsets_size > total - aligned ||
        response->data_size > SIZE_MAX || response->offsets_size > SIZE_MAX) return 0;
    return display_trace_reply(request->display_trace_code, data, (size_t)response->data_size,
                               data + aligned, (size_t)response->offsets_size, r);
}

static void display_trace_emit(struct display_trace_event *event) {
    static const char *const servers[] = {"unknown", "ids", "empty", "present", "null", "exception", "transport"};
    static const char *const translations[] = {"unknown", "ok", "present-ref", "null", "failed"};
    if (!display_trace_record(&display_trace_tracker, event)) return;
    struct timespec now = {0};
    int have_time = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    unsigned long long ms = have_time ? (unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL : 0;
    int index = event->code - 12;
    const char *server = event->server >= 0 && (size_t)event->server < sizeof servers / sizeof servers[0] ? servers[event->server] : "unknown";
    const char *translated = event->translated >= 0 && (size_t)event->translated < sizeof translations / sizeof translations[0] ? translations[event->translated] : "unknown";
    if (event->server == DISPLAY_EVENT_IDS)
        logf_("display-trace: mono_ms=%s%llu code=%d observations=%llu server=%s translated=%s count=%d first_id=%llu",
            have_time ? "" : "unavailable:", ms, event->code, (unsigned long long)display_trace_tracker.count[index],
            server, translated, event->count, (unsigned long long)event->first_id);
    else if (event->server == DISPLAY_EVENT_EXCEPTION || event->server == DISPLAY_EVENT_TRANSPORT)
        logf_("display-trace: mono_ms=%s%llu code=%d observations=%llu server=%s translated=%s result_code=%d",
            have_time ? "" : "unavailable:", ms, event->code, (unsigned long long)display_trace_tracker.count[index],
            server, translated, event->exception);
    else logf_("display-trace: mono_ms=%s%llu code=%d observations=%llu server=%s translated=%s",
        have_time ? "" : "unavailable:", ms, event->code, (unsigned long long)display_trace_tracker.count[index], server, translated);
    if (display_trace_tracker.transition_lines >= 16) display_trace_phase(PHASE_MATCH_LIMIT);
}

static void *zalloc(size_t n) { void *p = calloc(1, n); if (!p) { logf_("binderd64: out of memory"); exit(1); } return p; }

static void wq_push(struct wq *q, struct work *w) { w->next = 0; if (q->tail) q->tail->next = w; else q->head = w; q->tail = w; }
static struct work *wq_pop(struct wq *q) { struct work *w = q->head; if (w) { q->head = w->next; if (!q->head) q->tail = 0; } return w; }
static struct work *mkwork(int type) { struct work *w = zalloc(sizeof *w); w->type = type; return w; }

static void deliver(struct thread *th);
static void kick_proc(struct proc *p);

// ------------------------------------------------------------------------------------------- nodes and refs

static void node_work(struct node *n) {
    if (!n->owner || n->queued) return;
    int strong = n->internal_strong || n->local_strong, weak = n->nrefs || n->local_weak || strong;
    if ((weak && !n->has_weak) || (strong && !n->has_strong) || (!strong && n->has_strong) || (!weak && n->has_weak)) {
        struct work *w = mkwork(W_NODE); w->node = n; n->queued = 1;
        if (cur && cur->proc == n->owner) wq_push(&cur->todo, w);
        else { wq_push(&n->owner->todo, w); n->owner->kick = 1; kick_any = 1; }   /* delivered once the caller is done with the node */
    }
}

static void node_free_if_unused(struct node *n) {
    if (n->nrefs || n->internal_strong || n->local_strong || n->local_weak || n->queued || n->has_strong || n->has_weak) {
        if (n->owner) return;
        if (n->nrefs || n->local_strong || n->queued) return;     /* a dead node lives as long as someone refers to it */
    }
    if (n->owner) {
        struct node **pp = &n->owner->nodes[(n->ptr >> 4) % NHASH];
        while (*pp && *pp != n) pp = &(*pp)->hnext;
        if (*pp) *pp = n->hnext;
    }
    if (ctx_mgr == n) ctx_mgr = 0;
    for (struct work *w; (w = wq_pop(&n->async));) free(w);   /* the transactions themselves are released by the caller */
    free(n);
}

static struct node *node_get(struct proc *p, u64 ptr, u64 cookie, int create) {
    struct node **b = &p->nodes[(ptr >> 4) % NHASH];
    for (struct node *n = *b; n; n = n->hnext) if (n->ptr == ptr) {
        if (create && n->cookie != cookie) { V("  node %d ptr %llx: cookie %llx -> %llx", p->pid, (unsigned long long)ptr, (unsigned long long)n->cookie, (unsigned long long)cookie); n->cookie = cookie; }
        return n;
    }
    if (!create) return 0;
    struct node *n = zalloc(sizeof *n);
    n->owner = p; n->ptr = ptr; n->cookie = cookie; n->hnext = *b; *b = n;
    V("  new node %d ptr %llx cookie %llx", p->pid, (unsigned long long)ptr, (unsigned long long)cookie);
    return n;
}

static struct ref *ref_for_node(struct proc *p, struct node *n) {
    for (struct ref *r = n->refs; r; r = r->next_in_node) if (r->proc == p) return r;
    u32 d = n == ctx_mgr ? 0 : 1;
    for (; d < p->nrefs; d++) if (!p->refs[d] && (d || n == ctx_mgr)) break;
    if (d >= p->nrefs) {
        u32 nn = p->nrefs ? p->nrefs * 2 : 64; while (nn <= d) nn *= 2;
        p->refs = realloc(p->refs, nn * sizeof *p->refs);
        memset(p->refs + p->nrefs, 0, (nn - p->nrefs) * sizeof *p->refs);
        p->nrefs = nn;
    }
    struct ref *r = zalloc(sizeof *r);
    r->proc = p; r->desc = d; r->node = n; r->next_in_node = n->refs; n->refs = r; n->nrefs++;
    p->refs[d] = r;
    node_work(n);
    return r;
}

static struct ref *ref_get(struct proc *p, u32 desc) { return desc < p->nrefs ? p->refs[desc] : 0; }

static void ref_delete(struct ref *r) {
    struct node *n = r->node;
    struct ref **pp = &n->refs; while (*pp && *pp != r) pp = &(*pp)->next_in_node;
    if (*pp) *pp = r->next_in_node;
    n->nrefs--;
    if (r->strong) n->internal_strong--;
    r->proc->refs[r->desc] = 0;
    free(r);
    node_work(n);
    node_free_if_unused(n);
}

static void ref_inc(struct ref *r, int strong) {
    if (strong) { if (r->strong++ == 0) { r->node->internal_strong++; node_work(r->node); } }
    else r->weak++;
}

static void ref_dec(struct ref *r, int strong) {
    if (strong) {
        if (r->strong <= 0) return;
        if (--r->strong == 0) { r->node->internal_strong--; node_work(r->node); }
    } else { if (r->weak <= 0) return; r->weak--; }
    if (r->strong == 0 && r->weak == 0) ref_delete(r);
}

static void held_release(struct held *h, int n) {
    for (int i = 0; i < n; i++) {
        if (h[i].ref) ref_dec(h[i].ref, h[i].strong);
        else if (h[i].node) { h[i].node->local_strong--; node_work(h[i].node); node_free_if_unused(h[i].node); }
    }
    free(h);
}


// ------------------------------------------------------------------------------------------- transactions

static void txn_free(struct txn *t) {
    if (t->held) held_release(t->held, t->nheld);
    for (int i = 0; i < t->nfds; i++) close(t->fds[i]);
    free(t->fds);
    if (t->memfd >= 0) close(t->memfd);
    free(t->data);
    free(t);
}

static void thread_error(struct thread *th, u32 err) {
    struct work *w = mkwork(W_ERROR); w->err = err;
    wq_push(&th->todo, w);
    deliver(th);
}

/* the caller of t will never get an answer */
static void txn_fail(struct txn *t, u32 err) {
    binder_txn_trace_event(t->binder_txn_trace_id, "failed", 0, 0, 0, t->code, err);
    media_txn_trace_event(t->media_txn_trace_id, "failed", 0, 0, 0, t->code, err);
    if (t->display_trace_code) {
        struct display_trace_event event = { .code = t->display_trace_code, .server = DISPLAY_EVENT_TRANSPORT,
            .translated = DISPLAY_TRANSLATED_FAILED, .exception = (int)err };
        display_trace_emit(&event);
    }
    struct thread *from = t->from;
    if (from && from->stack == t) { from->stack = t->from_parent; thread_error(from, err); }
    txn_free(t);
}

static int translate(struct thread *th, struct proc *to, struct txn *t, char *data) {
    u64 n = t->offsets_size / 8, *offs = (u64 *)(data + ((t->data_size + 7) & ~7UL));
    struct proc *p = th->proc;
    t->held = zalloc((n + 2) * sizeof *t->held);
    int fdi = 0;
    for (u64 i = 0; i < n; i++) {
        if (offs[i] + sizeof(struct fbo) > t->data_size) return -1;
        struct fbo *o = (struct fbo *)(data + offs[i]);
        struct held *h = &t->held[t->nheld];
        switch (o->type) {
        case T_BINDER: case T_WEAK_BINDER: {
            struct node *nd = node_get(p, o->binder, o->cookie, 1);
            struct ref *r = ref_for_node(to, nd);
            int strong = o->type == T_BINDER;
            o->type = strong ? T_HANDLE : T_WEAK_HANDLE; o->binder = r->desc; o->cookie = 0;
            ref_inc(r, strong); h->ref = r; h->strong = strong; t->nheld++;
            break;
        }
        case T_HANDLE: case T_WEAK_HANDLE: {
            struct ref *r = ref_get(p, (u32)o->binder);
            int strong = o->type == T_HANDLE;
            if (!r) return -1;
            if (r->node->owner == to) {
                o->type = strong ? T_BINDER : T_WEAK_BINDER; o->binder = r->node->ptr; o->cookie = r->node->cookie;
                if (strong) { r->node->local_strong++; node_work(r->node); h->node = r->node; h->strong = 1; t->nheld++; }
            } else {
                struct ref *nr = ref_for_node(to, r->node);
                o->binder = nr->desc; o->cookie = 0;
                ref_inc(nr, strong); h->ref = nr; h->strong = strong; t->nheld++;
            }
            break;
        }
        case T_FD:
            if (fdi >= t->nfds) return -1;
            fdi++;
            break;
        case T_PTR:                         // hwbinder buffers: the two ends copy and re-point them themselves
            if (offs[i] + 40 > t->data_size) return -1;
            break;
        case T_FDA: {                       // …and an array of descriptors inside one of them
            if (offs[i] + 32 > t->data_size) return -1;
            u64 nf = ((u64 *)o)[1];
            if (nf > (u64)(t->nfds - fdi)) return -1;
            fdi += (int)nf;
            break;
        }
        default: return -1;
        }
    }
    return 0;
}

/* This verifies the daemon's translated reference only, not client deserialization. */
static int display_trace_held_reference(const struct txn *t, struct proc *to,
                                        const struct display_trace_result *r) {
    if (!r->binder_present) return 1;
    if (r->binder_type == T_HANDLE) {
        if (r->binder_value > UINT32_MAX) return 0;
        struct ref *ref = ref_get(to, (u32)r->binder_value);
        if (!ref || !ref->node || !ref->node->owner || ref->strong <= 0) return 0;
        for (int i = 0; i < t->nheld; i++) if (t->held[i].strong && t->held[i].ref == ref) return 1;
    } else if (r->binder_type == T_BINDER) {
        for (int i = 0; i < t->nheld; i++) {
            struct node *node = t->held[i].node;
            if (t->held[i].strong && node && node->owner == to && node->ptr == r->binder_value &&
                node->cookie == r->binder_cookie && node->local_strong > 0) return 1;
        }
    }
    return 0;
}

static void display_trace_reply_event(const struct txn *request, const struct txn *response, struct proc *to,
        int translated, int have_before, const struct display_trace_result *before,
        int have_after, const struct display_trace_result *after) {
    struct display_trace_event event = { .code = request->display_trace_code,
        .server = DISPLAY_EVENT_UNKNOWN, .translated = DISPLAY_TRANSLATED_UNKNOWN };
    if (have_before && before->state == DISPLAY_TRACE_EXCEPTION) {
        event.server = DISPLAY_EVENT_EXCEPTION; event.exception = before->exception;
    } else if (have_before && request->display_trace_code == 12) {
        event.server = before->count ? DISPLAY_EVENT_IDS : DISPLAY_EVENT_EMPTY;
        event.count = before->count; event.first_id = before->first_id;
    } else if (have_before) event.server = before->binder_present ? DISPLAY_EVENT_PRESENT : DISPLAY_EVENT_NULL;

    if (translated < 0) event.translated = DISPLAY_TRANSLATED_FAILED;
    else if (have_before && have_after && before->state == after->state) {
        if (before->state == DISPLAY_TRACE_EXCEPTION && before->exception == after->exception)
            event.translated = DISPLAY_TRANSLATED_OK;
        else if (before->state == DISPLAY_TRACE_OK && request->display_trace_code == 12 &&
                 before->count == after->count && before->first_id == after->first_id)
            event.translated = DISPLAY_TRANSLATED_OK;
        else if (before->state == DISPLAY_TRACE_OK && request->display_trace_code == 13 &&
                 before->binder_present == after->binder_present && display_trace_held_reference(response, to, after))
            event.translated = after->binder_present ? DISPLAY_TRANSLATED_PRESENT : DISPLAY_TRANSLATED_NULL;
    }
    display_trace_emit(&event);
}

static struct thread *ready_thread(struct proc *p) {
    for (struct thread *th = p->threads; th; th = th->next)
        if (th->waiting && !th->stack && !th->todo.head) return th;
    return 0;
}

static void kick_proc(struct proc *p) {
    struct thread *th;
    while (p->todo.head && (th = ready_thread(p))) deliver(th);
    if (p->todo.head) for (th = p->threads; th; th = th->next) if (!th->waiting) deliver(th);
}

static void queue_txn(struct txn *t, struct thread *target) {
    struct work *w = mkwork(W_TXN); w->t = t;
    if (target) wq_push(&target->todo, w);
    else wq_push(&t->to_proc->todo, w);
    binder_txn_trace_event(t->binder_txn_trace_id, t->reply ? "reply-queued" : "request-queued",
                           t->to_proc->pid, target ? target->tid : 0, 0, t->code, t->flags);
    media_txn_trace_event(t->media_txn_trace_id, t->reply ? "reply-queued" : "request-queued",
                          t->to_proc->pid, target ? target->tid : 0, 0, t->code, t->flags);
    if (target) deliver(target);
    else kick_proc(t->to_proc);
}

static void do_txn(struct thread *th, struct c_txn *m, char *payload, size_t payload_len, int *fds, int nfds) {
    struct proc *p = th->proc;
    struct txn *t = zalloc(sizeof *t);
    int reply = m->cmd == BC_REPLY;
    t->memfd = -1; t->code = m->code; t->flags = m->tflags; t->reply = reply;
    t->sender_pid = p->pid; t->sender_euid = m->h.euid;
    t->data_size = m->data_size; t->offsets_size = m->offsets_size; t->extra_size = m->extra_size;
    u64 total = ((t->data_size + 7) & ~7UL) + t->offsets_size;
    char *data;
    if (m->h.flags & F_BIG) {
        if (nfds < 1) { free(t); thread_error(th, BR_FAILED_REPLY); return; }
        t->memfd = fds[0]; fds++; nfds--;
        if (th->pid_guard_enabled) {
            int snapshot = strict_snapshot_memfd(t->memfd, t->data_size, t->offsets_size);
            close(t->memfd); t->memfd = snapshot;
            if (snapshot < 0) {
                for (int i = 0; i < nfds; i++) close(fds[i]);
                free(t); thread_error(th, BR_FAILED_REPLY); return;
            }
        }
        data = mmap(0, total ? total : 1, PROT_READ | PROT_WRITE, MAP_SHARED, t->memfd, 0);
        if (data == MAP_FAILED) { close(t->memfd); for (int i = 0; i < nfds; i++) close(fds[i]); free(t); thread_error(th, BR_FAILED_REPLY); return; }
    } else {
        if (total + t->extra_size > INLINE_MAX + 4096) { free(t); thread_error(th, BR_FAILED_REPLY); return; }
        data = t->data = malloc(total + t->extra_size + 8);
        memcpy(data, payload, total + t->extra_size);
    }
    if (nfds) { t->fds = malloc(nfds * sizeof(int)); memcpy(t->fds, fds, nfds * sizeof(int)); t->nfds = nfds; }

    struct thread *target_thread = 0; struct proc *to = 0; struct txn *in_reply_to = 0;
    u32 err = BR_FAILED_REPLY;
    if (reply) {
        in_reply_to = th->stack;
        if (!in_reply_to || in_reply_to->to_thread != th) goto fail;
        th->stack = in_reply_to->to_parent;
        target_thread = in_reply_to->from;
        if (!target_thread) {
            if (in_reply_to->display_trace_code) {
                struct display_trace_event event = { .code = in_reply_to->display_trace_code,
                    .server = DISPLAY_EVENT_TRANSPORT, .translated = DISPLAY_TRANSLATED_FAILED, .exception = (int)BR_DEAD_REPLY };
                display_trace_emit(&event);
            }
            err = BR_DEAD_REPLY; in_reply_to->from = 0; txn_free(in_reply_to); in_reply_to = 0; goto fail;
        }
        to = target_thread->proc;
    } else {
        struct node *n;
        if (m->handle) { struct ref *r = ref_get(p, m->handle); n = r ? r->node : 0; }
        else n = ctx_mgr;
        if (!n) goto fail;
        if (!n->owner) { err = BR_DEAD_REPLY; goto fail; }
        t->target = n; to = n->owner;
        if (!(t->flags & TF_ONE_WAY) && th->stack) {
            for (struct txn *x = th->stack; x; x = x->from_parent)
                if (x->from && x->from->proc == to) target_thread = x->from;
        }
    }
    t->to_proc = to;
    if (!reply && media_txn_trace_enabled && !(t->flags & TF_ONE_WAY) &&
        media_txn_trace_state.events < BMT_LIMIT && media_txn_process_match(p->pid, 0)) {
        t->media_txn_trace_id = bmt_start(&media_txn_trace_state, 1, 1, t->flags);
        media_txn_trace_event(t->media_txn_trace_id, "request", p->pid, th->tid, m->handle, t->code, t->flags);
        if (t->media_txn_trace_id && media_txn_trace_driver == 2 && t->code == 1 &&
            media_txn_trace_state.events < BMT_LIMIT && media_txn_process_match(to->pid, 1)) {
            struct hwget_class cls = hwget_classify(1, !(m->h.flags & F_BIG), data, payload_len,
                (size_t)t->data_size, (size_t)t->offsets_size, (size_t)t->extra_size);
            media_txn_trace_event(t->media_txn_trace_id, "hw-get-class", p->pid, th->tid,
                                  0, t->code, ((unsigned)cls.service << 8) | (unsigned)cls.instance);
        }
    }
    if (!reply && binder_txn_trace_enabled && !(t->flags & TF_ONE_WAY) &&
        binder_txn_trace_state.events < 256 && binder_txn_trace_match(p->pid, th->tid)) {
        t->binder_txn_trace_id = btt_start(&binder_txn_trace_state, 1, 1, 1, t->flags);
        binder_txn_trace_event(t->binder_txn_trace_id, "request", p->pid, th->tid, m->handle, t->code, t->flags);
        if (t->binder_txn_trace_id && t->code == 7) {
            int token = btt_token(1, t->code, display_trace_data_safe(t, total, payload_len), data,
                                  (size_t)t->data_size);
            binder_txn_trace_event(t->binder_txn_trace_id, "request-token", 0, 0, 0, t->code, (unsigned)token);
        }
    }
    if (!reply && display_trace_enabled && (t->code == 12 || t->code == 13) && !(t->flags & TF_ONE_WAY)) {
        display_trace_phase(PHASE_CANDIDATE);
        if (t->sender_euid != 1000) display_trace_phase(PHASE_UID_OTHER);
        else {
            display_trace_phase(PHASE_UID_MATCH);
            if (t->memfd >= 0) display_trace_phase(PHASE_BIG);
            else if (!display_trace_data_safe(t, total, payload_len) || t->data_size > SIZE_MAX)
                display_trace_phase(PHASE_INLINE_INVALID);
            else if (!display_trace_request(data, (size_t)t->data_size)) display_trace_phase(PHASE_INTERFACE_OTHER);
            else {
                display_trace_phase(PHASE_INTERFACE_MATCH);
                enum display_trace_identity_result source = display_trace_process(p->pid, 0);
                enum display_trace_identity_result target = display_trace_process(to->pid, 1);
                display_trace_identity_phase(0, source);
                display_trace_identity_phase(1, target);
                if (display_trace_match(1, t->code, t->flags, t->sender_euid,
                        source == DISPLAY_ID_MATCH, target == DISPLAY_ID_MATCH, data, (size_t)t->data_size)) {
                    t->display_trace_code = (int)t->code;
                }
            }
        }
    }
    int trace_reply = reply && in_reply_to && in_reply_to->display_trace_code;
    struct display_trace_result before = {0}, after = {0};
    int have_before = trace_reply && display_trace_decode(in_reply_to, t, data, total, payload_len, &before);
    int translate_result = translate(th, to, t, data);
    if (trace_reply) {
        int have_after = translate_result == 0 && display_trace_decode(in_reply_to, t, data, total, payload_len, &after);
        display_trace_reply_event(in_reply_to, t, to, translate_result, have_before, &before, have_after, &after);
        if (translate_result < 0) in_reply_to->display_trace_code = 0; /* already counted above */
    }
    if (translate_result < 0) { if (t->memfd >= 0) munmap(data, total ? total : 1); goto fail; }
    if (t->memfd >= 0) munmap(data, total ? total : 1);
    if (t->target) {                        /* the target stays alive until the buffer is freed */
        t->target->local_strong++; node_work(t->target);
        t->held[t->nheld].node = t->target; t->held[t->nheld++].strong = 1;
    }

    struct work *done = mkwork(W_COMPLETE);
    if (reply) {
        t->binder_txn_trace_id = in_reply_to->binder_txn_trace_id;
        t->media_txn_trace_id = in_reply_to->media_txn_trace_id;
        target_thread->stack = in_reply_to->from_parent;
        in_reply_to->from = 0; txn_free(in_reply_to);
        wq_push(&th->todo, done);
        queue_txn(t, target_thread);
    } else if (!(t->flags & TF_ONE_WAY)) {
        t->need_reply = 1; t->from = th; t->from_parent = th->stack; th->stack = t;
        done->deferred = 1;                 /* goes out together with the reply or a nested call */
        wq_push(&th->todo, done);
        queue_txn(t, target_thread);
    } else {
        wq_push(&th->todo, done);
        if (t->target->has_async) { struct work *w = mkwork(W_TXN); w->t = t; wq_push(&t->target->async, w); }
        else { t->target->has_async = 1; queue_txn(t, 0); }
    }
    return;
fail:
    binder_txn_trace_event(t->binder_txn_trace_id, "failed", p->pid, th->tid, m->handle, t->code, err);
    media_txn_trace_event(t->media_txn_trace_id, "failed", p->pid, th->tid, m->handle, t->code, err);
    V("txn failed: pid %d handle %u code %u err %x", p->pid, m->handle, m->code, err);
    if (t->display_trace_code) {
        struct display_trace_event event = { .code = t->display_trace_code, .server = DISPLAY_EVENT_TRANSPORT,
            .translated = DISPLAY_TRANSLATED_FAILED, .exception = (int)err };
        display_trace_emit(&event);
    }
    txn_free(t);
    if (reply && in_reply_to) { th->stack = in_reply_to->to_parent; txn_fail(in_reply_to, BR_FAILED_REPLY); }
    thread_error(th, err);
}

// ------------------------------------------------------------------------------------------- delivery

static int emit_node(struct node *n, u32 *br) {
    int k = 0, strong = n->internal_strong || n->local_strong, weak = n->nrefs || n->local_weak || strong;
#define PUT(c) do { V("  node %d ptr %llx cookie %llx: %x", n->owner ? n->owner->pid : 0, (unsigned long long)n->ptr, (unsigned long long)n->cookie, (unsigned)c); br[k] = c; memcpy(br + k + 1, &n->ptr, 8); memcpy(br + k + 3, &n->cookie, 8); k += 5; } while (0)
    /* the owner must not be told to drop a reference before it has taken it: until BC_*_DONE the node holds one itself */
    if (weak && !n->has_weak) { PUT(BR_INCREFS); n->has_weak = 1; n->pend_weak = 1; n->local_weak++; }
    if (strong && !n->has_strong) { PUT(BR_ACQUIRE); n->has_strong = 1; n->pend_strong = 1; n->local_strong++; }
    if (!strong && n->has_strong) { PUT(BR_RELEASE); n->has_strong = 0; }
    if (!weak && n->has_weak) { PUT(BR_DECREFS); n->has_weak = 0; }
#undef PUT
    return k;
}

static int deliverable(struct thread *th) {
    for (struct work *w = th->todo.head; w; w = w->next) if (!w->deferred) return 1;
    return !th->stack && !th->todo.head && th->proc->todo.head;
}

static void deliver(struct thread *th) {
    if (!th->waiting) {
        /* a looper that polls the descriptor instead of sitting in read (healthd): one empty message wakes it */
        if ((th->looper & 2) && !th->woken && deliverable(th)) {
            struct d_hdr h = { 0 }; h.flags = F_WAKE; th->woken = 1;
            send(th->fd, &h, sizeof h, MSG_DONTWAIT | MSG_NOSIGNAL);
        }
        return;
    }
    if (!deliverable(th)) {
        if (!th->nonblock) return;
        if (th->todo.head) { for (struct work *w = th->todo.head; w; w = w->next) w->deferred = 0; }
        else { struct d_hdr h = { 0 }; h.flags = F_AGAIN; th->waiting = 0; send(th->fd, &h, sizeof h, MSG_DONTWAIT | MSG_NOSIGNAL); return; }
    }
    struct proc *p = th->proc;
    u32 br[64]; int k = 0;
    int room = (int)(th->read_size / 4) - 1 - 17 - 1;     /* BR_NOOP, one transaction, BR_SPAWN_LOOPER */
    if (room < 20) room = 20;
    if (room > 40) room = 40;
    struct txn *t = 0;
    struct wq *q = th->todo.head ? &th->todo : &p->todo;
    int stop = 0;
    while (q->head && !t && !stop) {
        struct work *w = q->head;
        if (w->type != W_TXN && k + 20 > room) break;
        wq_pop(q);
        switch (w->type) {
        case W_COMPLETE: br[k++] = BR_TRANSACTION_COMPLETE; break;
        case W_ERROR: br[k++] = w->err; break;
        case W_NODE: w->node->queued = 0; k += emit_node(w->node, br + k); node_free_if_unused(w->node); break;
        case W_DEAD_BINDER: br[k] = BR_DEAD_BINDER; memcpy(br + k + 1, &w->cookie, 8); k += 3;
            stop = 1;       /* the death recipient may make calls of its own: nothing may follow it in this batch (as in the driver) */
            break;
        case W_CLEAR_DONE: br[k] = BR_CLEAR_DEATH_NOTIFICATION_DONE; memcpy(br + k + 1, &w->cookie, 8); k += 3; break;
        case W_TXN: t = w->t; break;
        }
        free(w);
    }
    th->waiting = 0;
    if ((th->looper & 3) && !p->requested && p->started < p->max_threads && !ready_thread(p)) {
        p->requested++; memmove(br + 1, br, k * 4); br[0] = BR_SPAWN_LOOPER; k++;
    }
    struct d_hdr h = { 0 };
    struct iovec iov[3]; int ni = 0;
    char cbuf[CMSG_SPACE(sizeof(int) * 254)];
    struct msghdr mh = { 0 };
    int pass[254], np = 0;
    h.br_size = (u32)k * 4;
    iov[ni].iov_base = &h; iov[ni++].iov_len = sizeof h;
    if (k) { iov[ni].iov_base = br; iov[ni++].iov_len = h.br_size; }
    if (t) {
        struct dbuf *b = zalloc(sizeof *b);
        b->proc = p; b->held = t->held; b->nheld = t->nheld; t->held = 0; t->nheld = 0;
        b->next = p->bufs; if (p->bufs) p->bufs->prev = b; p->bufs = b;
        h.flags |= F_TXN; h.cmd = t->reply ? BR_REPLY : BR_TRANSACTION;
        h.code = t->code; h.tflags = t->flags; h.sender_pid = t->from || t->reply ? t->sender_pid : 0; h.sender_euid = t->sender_euid;
        if (t->target) { h.ptr = t->target->ptr; h.cookie = t->target->cookie; }
        h.data_size = t->data_size; h.offsets_size = t->offsets_size; h.extra_size = t->extra_size; h.id = (u64)(uintptr_t)b;
        if (t->memfd >= 0) { h.flags |= F_BIG; pass[np++] = t->memfd; }
        else if (t->data_size || t->offsets_size) { iov[ni].iov_base = t->data; iov[ni++].iov_len = ((t->data_size + 7) & ~7UL) + t->offsets_size + t->extra_size; }
        for (int i = 0; i < t->nfds && np < 254; i++) pass[np++] = t->fds[i];
        h.nfds = (u32)t->nfds;
        if (!t->reply && (t->flags & TF_ONE_WAY)) b->async_node = t->target;
    }
    mh.msg_iov = iov; mh.msg_iovlen = ni;
    if (np) {
        mh.msg_control = cbuf; mh.msg_controllen = CMSG_SPACE(sizeof(int) * np);
        struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int) * np);
        memcpy(CMSG_DATA(c), pass, sizeof(int) * np);
    }
    V("  -> %d/%d: br %u words%s%s", p->pid, th->tid, (u32)k, t ? (t->reply ? " + reply" : " + transaction") : "", np ? " + fds" : "");
    ssize_t sent = sendmsg(th->fd, &mh, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent < 0) V("send to %d/%d failed: %s", p->pid, th->tid, strerror(errno));
    if (t) binder_txn_trace_event(t->binder_txn_trace_id,
        sent < 0 ? "send-failed" : t->reply ? "reply-sent" : "request-sent",
        p->pid, th->tid, 0, t->code, t->flags);
    if (t) media_txn_trace_event(t->media_txn_trace_id,
        sent < 0 ? "send-failed" : t->reply ? "reply-sent" : "request-sent",
        p->pid, th->tid, 0, t->code, t->flags);
    /* a polling looper goes back to poll after this batch: work that did not fit into it (a call queued for the
       process while the thread's own queue had something) must make the descriptor readable again */
    int more = (th->looper & 2) && !(t && t->need_reply) && deliverable(th);
    if (more) { struct d_hdr wk = { 0 }; wk.flags = F_WAKE; th->woken = 1; send(th->fd, &wk, sizeof wk, MSG_DONTWAIT | MSG_NOSIGNAL); }
    if (t) {
        if (t->need_reply) { t->to_parent = th->stack; t->to_thread = th; th->stack = t;
            for (int i = 0; i < t->nfds; i++) close(t->fds[i]);
            free(t->fds); t->fds = 0; t->nfds = 0;
            if (t->memfd >= 0) { close(t->memfd); t->memfd = -1; }
            free(t->data); t->data = 0;
        } else txn_free(t);
    }
}

// ------------------------------------------------------------------------------------------- commands

static void buf_free(struct proc *p, struct dbuf *b) {
    if (b->prev) b->prev->next = b->next; else p->bufs = b->next;
    if (b->next) b->next->prev = b->prev;
    if (b->async_node) {
        struct node *n = b->async_node;
        struct work *w = wq_pop(&n->async);
        if (w) { if (n->owner) { wq_push(&n->owner->todo, w); kick_proc(n->owner); } else { txn_free(w->t); free(w); } }
        else n->has_async = 0;
    }
    held_release(b->held, b->nheld);
    free(b);
}

static void do_bcs(struct thread *th, u32 *c, int words) {
    struct proc *p = th->proc;
    for (int i = 0; i < words;) {
        u32 cmd = c[i++]; u64 a = 0, b = 0;
        int sz = (int)((cmd >> 16) & 0x3fff);
        if (i * 4 + sz > words * 4) break;
        if (sz == 4) a = c[i];
        else if (sz == 12) { a = c[i]; memcpy(&b, c + i + 1, 8); }       /* handle + cookie, packed */
        else if (sz >= 8) { memcpy(&a, c + i, 8); if (sz == 16) memcpy(&b, c + i + 2, 8); }
        i += (sz + 3) / 4;
        switch (cmd) {
        case BC_INCREFS: case BC_ACQUIRE: {
            struct ref *r = ref_get(p, (u32)a);
            if (!r && (u32)a == 0 && ctx_mgr) r = ref_for_node(p, ctx_mgr);
            /* a strong reference cannot be made out of a weak one once the object has no strong references left
             * (its owner may have destroyed it); the driver refuses this the same way */
            if (r && cmd == BC_ACQUIRE && !r->strong && !r->node->internal_strong && r->node != ctx_mgr) { V("pid %d: acquire of a released node refused", p->pid); break; }
            if (r) ref_inc(r, cmd == BC_ACQUIRE);
            break;
        }
        case BC_RELEASE: case BC_DECREFS: { struct ref *r = ref_get(p, (u32)a); if (r) ref_dec(r, cmd == BC_RELEASE); break; }
        case BC_INCREFS_DONE: case BC_ACQUIRE_DONE: {
            struct node *n = node_get(p, a, b, 0);
            if (!n) break;
            if (cmd == BC_ACQUIRE_DONE && n->pend_strong) { n->pend_strong = 0; n->local_strong--; }
            if (cmd == BC_INCREFS_DONE && n->pend_weak) { n->pend_weak = 0; n->local_weak--; }
            node_work(n); node_free_if_unused(n);
            break;
        }
        case BC_REGISTER_LOOPER: th->looper |= 1; if (p->requested) { p->requested--; p->started++; } break;
        case BC_ENTER_LOOPER: th->looper |= 2; break;
        case BC_EXIT_LOOPER: th->looper |= 4; break;
        case BC_REQUEST_DEATH_NOTIFICATION: {
            struct ref *r = ref_get(p, (u32)a);
            if (!r) break;
            r->has_death = 1; r->death = b;
            if (!r->node->owner) { struct work *w = mkwork(W_DEAD_BINDER); w->cookie = b; wq_push(&p->todo, w); kick_proc(p); }
            break;
        }
        case BC_CLEAR_DEATH_NOTIFICATION: {
            struct ref *r = ref_get(p, (u32)a);
            if (r) r->has_death = 0;
            struct work *w = mkwork(W_CLEAR_DONE); w->cookie = b; wq_push(&th->todo, w);
            break;
        }
        case BC_DEAD_BINDER_DONE: break;
        case BC_AEMU_FREE: { struct dbuf *d = (struct dbuf *)(uintptr_t)a; if (d && d->proc == p) buf_free(p, d); break; }
        case BC_AEMU_MAX_THREADS: p->max_threads = (int)a; break;
        case BC_AEMU_CTX_MGR: if (!ctx_mgr) { ctx_mgr = node_get(p, 0, 0, 1); ctx_mgr->local_strong++; ctx_mgr->has_strong = ctx_mgr->has_weak = 1; } break;
        default: V("pid %d: unknown command %x", p->pid, cmd); return;
        }
    }
}

// ------------------------------------------------------------------------------------------- lifetime

static void thread_release(struct thread *th) {
    struct proc *p = th->proc;
    for (struct txn *t = th->stack; t;) {
        struct txn *nx;
        if (t->to_thread == th) {           /* we were serving it: the caller gets a dead reply */
            nx = t->to_parent; t->to_thread = 0;
            struct thread *from = t->from;
            if (from && from->stack == t) { from->stack = t->from_parent; thread_error(from, BR_DEAD_REPLY); }
            t->from = 0; txn_free(t);
        } else {                            /* we were waiting for its answer: nobody to answer to any more */
            nx = t->from_parent; t->from = 0;
            if (!t->to_thread) { /* still queued: stays there and is dropped at reply time */ }
        }
        t = nx;
    }
    th->stack = 0;
    for (struct work *w; (w = wq_pop(&th->todo));) {
        if (w->type == W_TXN) txn_fail(w->t, BR_DEAD_REPLY);
        else if (w->type == W_NODE) { w->node->queued = 0; if (p) node_work(w->node); }
        free(w);
    }
    if (cur == th) cur = 0;
    if (p) { struct thread **pp = &p->threads; while (*pp && *pp != th) pp = &(*pp)->next; if (*pp) *pp = th->next; }
    epoll_ctl(ep, EPOLL_CTL_DEL, th->fd, 0);
    if (th->pid_guard_enabled) bpg_close(&th->pid_guard);
    close(th->fd);
    free(th);
}

static void proc_release(struct proc *p) {
    V("pid %d died", p->pid);
    struct proc **pp = &procs; while (*pp && *pp != p) pp = &(*pp)->next; if (*pp) *pp = p->next;
    while (p->threads) thread_release(p->threads);
    for (struct work *w; (w = wq_pop(&p->todo));) {
        if (w->type == W_TXN) txn_fail(w->t, BR_DEAD_REPLY);
        else if (w->type == W_NODE) w->node->queued = 0;
        free(w);
    }
    while (p->bufs) buf_free(p, p->bufs);
    /* calls still queued behind one-way calls hold references of this process: drop them first */
    for (int i = 0; i < NHASH; i++) for (struct node *n = p->nodes[i]; n; n = n->hnext)
        for (struct work *w; (w = wq_pop(&n->async));) { txn_free(w->t); free(w); }
    for (u32 i = 0; i < p->nrefs; i++) if (p->refs[i]) ref_delete(p->refs[i]);
    free(p->refs);
    for (int i = 0; i < NHASH; i++) for (struct node *n = p->nodes[i], *nx; n; n = nx) {
        nx = n->hnext;
        n->owner = 0; n->queued = 0; n->hnext = 0;
        if (n->pend_strong) { n->pend_strong = 0; n->local_strong--; }
        if (n->pend_weak) { n->pend_weak = 0; n->local_weak--; }
        for (struct work *w; (w = wq_pop(&n->async));) { txn_free(w->t); free(w); }
        for (struct ref *r = n->refs; r; r = r->next_in_node) if (r->has_death) {
            struct work *w = mkwork(W_DEAD_BINDER); w->cookie = r->death; wq_push(&r->proc->todo, w);
        }
        if (ctx_mgr == n) ctx_mgr = 0;
        if (!n->nrefs && !n->local_strong) free(n);
    }
    struct proc *q, *qn;
    for (q = procs; q; q = qn) { qn = q->next; kick_proc(q); }
    free(p);
}

static void kick_pending(void) {
    while (kick_any) {
        kick_any = 0;
        for (struct proc *q = procs; q; q = q->next) if (q->kick) { q->kick = 0; kick_proc(q); }
    }
}

/* Strict-only private snapshot of a large Binder payload. */
static int strict_snapshot_memfd(int input, u64 data_size, u64 offsets_size) {
    if (input < 0 || data_size > UINT64_MAX - 7 || (offsets_size & 7)) return -1;
    u64 aligned = (data_size + 7) & ~7ULL;
    if (aligned > UINT64_MAX - offsets_size) return -1;
    u64 total = aligned + offsets_size;
    if (!total || total > 16ULL * 1024 * 1024) return -1;
    u64 needed = offsets_size ? total : data_size;
    int output = (int)syscall(SYS_memfd_create, "aemu-binder-snapshot", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (output < 0) return -1;
    if (ftruncate(output, (off_t)total) != 0) goto fail;
    char buffer[8192];
    for (u64 at = 0; at < needed;) {
        size_t wanted = needed - at < sizeof buffer ? (size_t)(needed - at) : sizeof buffer;
        ssize_t count = pread(input, buffer, wanted, (off_t)at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) goto fail;
        size_t done = 0;
        while (done < (size_t)count) {
            ssize_t written = pwrite(output, buffer + done, (size_t)count - done, (off_t)(at + done));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) goto fail;
            done += (size_t)written;
        }
        at += (u64)count;
    }
    if (fcntl(output, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0) goto fail;
    return output;
fail:
    close(output);
    return -1;
}

/* Strict mode validates the packet envelope before Binder reads any fields.
 * Big scatter-gather messages stay unsupported until their mmap path is fixed. */
static int strict_frame_shape(const char *data, size_t length, const int *fds, int nfds) {
    if (length < sizeof(struct c_hdr)) return 0;
    const struct c_hdr *h = (const struct c_hdr *)data;
    if (h->op == OP_HELLO) {
        if (length != sizeof(struct c_hello) || nfds) return 0;
        const struct c_hello *m = (const struct c_hello *)data;
        return m->main <= 1 && m->tid > 0 && m->tid <= INT32_MAX;
    }
    if (h->op == OP_READ) return length == sizeof(*h) && nfds == 0;
    if (h->op == OP_BCS) return nfds == 0 && (length - sizeof(*h)) % sizeof(u32) == 0;
    if (h->op != OP_TXN || length < sizeof(struct c_txn)) return 0;
    const struct c_txn *m = (const struct c_txn *)data;
    if (m->cmd != BC_TRANSACTION && m->cmd != BC_REPLY) return 0;
    if (m->data_size > UINT64_MAX - 7 || (m->offsets_size & 7)) return 0;
    u64 aligned = (m->data_size + 7) & ~7ULL;
    if (aligned > UINT64_MAX - m->offsets_size) return 0;
    u64 total = aligned + m->offsets_size;
    if (total > UINT64_MAX - m->extra_size || total + m->extra_size > 16ULL * 1024 * 1024) return 0;
    if (h->flags & F_BIG) {
        struct stat st;
        /* The copy path reads only data_size if no offsets follow; its own
         * sealed snapshot supplies zero padding before Binder maps it. */
        u64 needed = m->offsets_size ? total : m->data_size;
        return nfds >= 1 && !m->extra_size && length == sizeof(*m) &&
               fstat(fds[0], &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 && (u64)st.st_size >= needed;
    }
    if (total + m->extra_size > INLINE_MAX + 4096) return 0;
    /* bclient only adds alignment bytes if offsets or extra data follow. */
    u64 wire = (m->offsets_size || m->extra_size ? aligned : m->data_size) + m->offsets_size + m->extra_size;
    return wire <= SIZE_MAX && length - sizeof(*m) == (size_t)wire;
}

static void strict_drop(struct thread *th, int *fds, int nfds) {
    for (int i = 0; i < nfds; i++) close(fds[i]);
    if (th->is_main && th->proc) proc_release(th->proc); else thread_release(th);
    kick_pending();
}

/* On strict registration failure close both the connected socket and pidfd. */
static int watch_thread(int epoll_fd, struct thread *th) {
    struct epoll_event event = { .events = EPOLLIN, .data.ptr = th };
    int result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, th->fd, &event);
    if (result != 0 && th->pid_guard_enabled) {
        bpg_close(&th->pid_guard); close(th->fd); free(th);
    }
    return result;
}

static int strict_collect_rights(struct msghdr *message, int *fds, int *nfds) {
    int valid = 1;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(message); c; c = CMSG_NXTHDR(message, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        if (c->cmsg_len < CMSG_LEN(0)) { valid = 0; continue; }
        size_t bytes = c->cmsg_len - CMSG_LEN(0), count = bytes / sizeof(int);
        if (bytes % sizeof(int)) valid = 0;
        for (size_t i = 0; i < count; i++) {
            int received; memcpy(&received, (char *)CMSG_DATA(c) + i * sizeof(int), sizeof received);
            if (*nfds < 254) fds[(*nfds)++] = received; else { close(received); valid = 0; }
        }
    }
    return valid;
}

static void on_message(struct thread *th) {
    static char buf[INLINE_MAX + 4096 + 512];
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int) * 254) + CMSG_SPACE(sizeof(struct ucred))]; } cbuf;
    struct iovec iov = { buf, sizeof buf };
    struct msghdr mh = { 0 };
    mh.msg_iov = &iov; mh.msg_iovlen = 1; mh.msg_control = cbuf.bytes;
    mh.msg_controllen = th->pid_guard_enabled ? sizeof cbuf.bytes : CMSG_SPACE(sizeof(int) * 254);
    ssize_t n = recvmsg(th->fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
    if (n <= 0) {
        if (n == 0 && th->pid_guard_enabled) {
            int received[254], count = 0;
            strict_collect_rights(&mh, received, &count);
            for (int i = 0; i < count; i++) close(received[i]);
        }
        if (th->is_main && th->proc) proc_release(th->proc); else thread_release(th);
        kick_pending();
        return;
    }
    int fds[254], nfds = 0;
    int valid_rights = 1;
    if (th->pid_guard_enabled) valid_rights = strict_collect_rights(&mh, fds, &nfds);
    else {
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                nfds = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
                memcpy(fds, CMSG_DATA(c), nfds * sizeof(int));
            }
        }
    }
    if (th->pid_guard_enabled) {
        if (!valid_rights || !bpg_packet(&th->pid_guard, &mh) ||
            !strict_frame_shape(buf, (size_t)n, fds, nfds)) {
            strict_drop(th, fds, nfds); return;
        }
        /* Pad unaligned inline data with zeroes: the sender can omit these
         * bytes when no offsets or scatter-gather payload follows. */
        struct c_hdr *header = (struct c_hdr *)buf;
        if (header->op == OP_TXN && !(header->flags & F_BIG)) {
            struct c_txn *m = (struct c_txn *)buf;
            if (!m->offsets_size && !m->extra_size && (m->data_size & 7)) {
                size_t padding = 8 - (size_t)(m->data_size & 7);
                if ((size_t)n + padding > sizeof buf) { strict_drop(th, fds, nfds); return; }
                memset(buf + n, 0, padding);
            }
        }
    }
    struct c_hdr *h = (struct c_hdr *)buf;
    if ((size_t)n < sizeof *h) return;
    if (h->op == OP_HELLO) {
        struct c_hello *m = (struct c_hello *)buf;
        if (th->pid_guard_enabled && th->proc) { strict_drop(th, fds, nfds); return; }
        /* every open("/dev/binder") is a process of its own, as in the driver; its threads name it by pid + descriptor */
        struct proc *p = procs;
        int peer_pid = th->pid_guard_enabled ? (int)th->pid_guard.pid : (int)m->pid;
        while (p && !(p->pid == peer_pid && p->key == m->pad)) p = p->next;
        if (th->pid_guard_enabled) {
            int has_main = 0;
            if (p) for (struct thread *t = p->threads; t; t = t->next) if (t->is_main) { has_main = 1; break; }
            if ((!m->main && !has_main) || (m->main && p)) { strict_drop(th, fds, nfds); return; }
        }
        if (m->main || !p) {
            p = zalloc(sizeof *p); p->pid = peer_pid; p->key = m->pad; p->next = procs; procs = p;
            V("pid %d opened binder (fd %u)", p->pid, p->key);
        }
        th->proc = p; th->tid = (int)m->tid; th->is_main = (int)m->main;
        th->next = p->threads; p->threads = th;
        return;
    }
    if (!th->proc) {
        if (th->pid_guard_enabled) strict_drop(th, fds, nfds);
        return;
    }
    cur = th;
    V("%d/%d: op %u flags %u len %zd fds %d", th->proc->pid, th->tid, h->op, h->flags, n, nfds);
    if (h->op == OP_BCS) do_bcs(th, (u32 *)(h + 1), (int)((n - sizeof *h) / 4));
    else if (h->op == OP_TXN && (size_t)n >= sizeof(struct c_txn)) { do_txn(th, (struct c_txn *)buf, buf + sizeof(struct c_txn), (size_t)n - sizeof(struct c_txn), fds, nfds); nfds = 0; }
    for (int i = 0; i < nfds; i++) close(fds[i]);
    if (h->flags & F_READ) {
        th->woken = 0; th->waiting = 1; th->nonblock = !!(h->flags & F_NONBLOCK); th->read_size = h->read_size;
        deliver(th);
        if (th->waiting && !th->stack && !th->todo.head) kick_proc(th->proc);
    }
    cur = 0;
    kick_pending();
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: binderd64 <socket>\n"); return 2; }
    verbose = getenv("AEMU_BINDER_VERBOSE") != 0;
    const char *strict = getenv("AEMU_BINDER_STRICT_PID");
    binder_strict_pid = strict && !strcmp(strict, "1");
    if (binder_strict_pid && bpg_probe() != 0) {
        perror("binderd64: strict PID unavailable"); return 1;
    }
    display_trace_enabled = getenv("AEMU_BINDER_DISPLAY_TRACE") != 0;
    binder_txn_trace_enabled = getenv("AEMU_BINDER_TXN_TRACE") != 0;
    const char *media_trace = getenv("AEMU_MEDIA_BINDER_TRACE");
    media_txn_trace_driver = media_trace && !strcmp(media_trace, "main") ? 1 :
                             media_trace && !strcmp(media_trace, "hw") ? 2 : 0;
    media_txn_trace_enabled = media_txn_trace_driver != 0;
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stderr, 0, _IOLBF, 0);
    if (display_trace_enabled) logf_("display-trace: enabled; exact process identity required; no matching output means unobserved");
    struct rlimit rl; if (!getrlimit(RLIMIT_NOFILE, &rl)) { rl.rlim_cur = rl.rlim_max; setrlimit(RLIMIT_NOFILE, &rl); }
    int ls = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (binder_strict_pid) {
        int passcred = 1;
        if (setsockopt(ls, SOL_SOCKET, SO_PASSCRED, &passcred, sizeof passcred) != 0) {
            perror("binderd64: strict credentials"); close(ls); return 1;
        }
    }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    strncpy(sa.sun_path, argv[1], sizeof sa.sun_path - 1);
    unlink(argv[1]);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(ls, 128) < 0) { perror("binderd64: bind"); return 1; }
    ep = epoll_create1(EPOLL_CLOEXEC);
    if (binder_strict_pid && ep < 0) { perror("binderd64: strict epoll"); close(ls); return 1; }
    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = 0 };
    int listener_watch = epoll_ctl(ep, EPOLL_CTL_ADD, ls, &ev);
    if (binder_strict_pid && listener_watch != 0) { perror("binderd64: strict listener watch"); close(ls); close(ep); return 1; }
    logf_("binderd64: listening on %s", argv[1]);
    for (;;) {
        struct epoll_event evs[64];
        int n = epoll_wait(ep, evs, 64, -1);
        for (int i = 0; i < n; i++) {
            if (!evs[i].data.ptr) {
                int fd = accept4(ls, 0, 0, SOCK_CLOEXEC);
                if (fd < 0) continue;
                struct thread *th = zalloc(sizeof *th);
                th->fd = fd;
                if (binder_strict_pid) {
                    if (bpg_init(&th->pid_guard, fd) != 0) { close(fd); free(th); continue; }
                    th->pid_guard_enabled = 1;
                }
                watch_thread(ep, th);
            } else {
                on_message(evs[i].data.ptr);
                break;      /* the handler may have freed threads named by the remaining events: poll again */
            }
        }
    }
}
