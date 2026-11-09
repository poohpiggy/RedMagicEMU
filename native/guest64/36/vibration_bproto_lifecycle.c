#include "vibration_bproto_lifecycle.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BR_TRANSACTION 0x80407202u
#define BR_REPLY 0x80407203u
#define BR_TRANSACTION_COMPLETE 0x00007206u
#define BR_INCREFS 0x80107207u
#define BR_ACQUIRE 0x80107208u
#define BR_RELEASE 0x80107209u
#define BR_DECREFS 0x8010720au
#define BC_INCREFS_DONE 0x40106308u
#define BC_ACQUIRE_DONE 0x40106309u
#define BC_ENTER_LOOPER 0x0000630cu
#define BC_EXIT_LOOPER 0x0000630du

static uint32_t read32(const void *p) { uint32_t n; memcpy(&n, p, sizeof n); return n; }
static uint64_t read64(const void *p) { uint64_t n; memcpy(&n, p, sizeof n); return n; }
static void put32(void *p, uint32_t n) { memcpy(p, &n, sizeof n); }
static void put64(void *p, uint64_t n) { memcpy(p, &n, sizeof n); }

static int bad(struct rme_vib_lifecycle *l) {
    if (l && l->client) rme_bproto_close(l->client);
    errno = EPROTO;
    return -1;
}

static int send_commands(struct rme_vib_lifecycle *l, const void *data, size_t size) {
    if (!l || !l->client || l->client->fd < 0 || !data || !size) { errno = EINVAL; return -1; }
    struct c_hdr h = { .op = OP_BCS, .euid = l->client->guest_uid };
    struct iovec vec[] = {{ &h, sizeof h }, { (void *)data, size }};
    struct msghdr msg = { .msg_iov = vec, .msg_iovlen = 2 };
    ssize_t n;
    do { n = sendmsg(l->client->fd, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    if (n < 0 || (size_t)n != sizeof h + size) {
        int error = n < 0 ? errno : EIO;
        rme_bproto_close(l->client); errno = error; return -1;
    }
    return 0;
}

void rme_vib_lifecycle_init(struct rme_vib_lifecycle *l, struct rme_bproto_client *c, uint64_t ptr, uint64_t cookie) {
    if (l) *l = (struct rme_vib_lifecycle){ .client = c, .ptr = ptr, .cookie = cookie };
}

int rme_vib_lifecycle_enter(struct rme_vib_lifecycle *l) {
    if (!l || l->entered) { errno = EINVAL; return -1; }
    uint32_t cmd = BC_ENTER_LOOPER;
    if (send_commands(l, &cmd, sizeof cmd) != 0) return -1;
    l->entered = 1; return 0;
}

int rme_vib_lifecycle_observe(struct rme_vib_lifecycle *l, const struct d_hdr *h,
                              const void *payload, size_t size) {
    if (!l || !l->client || l->client->fd < 0 || !h || (size && !payload)) { errno = EINVAL; return -1; }
    if (h->br_size > 4096 || h->br_size % 4 || h->br_size > size || h->nfds || h->extra_size ||
        (h->flags & ~(F_TXN | F_AGAIN | F_WAKE)) || (h->offsets_size % 8) ||
        ((h->flags & F_WAKE) && (h->flags & F_AGAIN)) ||
        ((h->flags & (F_WAKE | F_AGAIN)) && h->br_size) ||
        ((h->flags & F_TXN) && (h->flags & (F_WAKE | F_AGAIN)))) return bad(l);
    if (h->flags & F_TXN) {
        if (l->pending_id || !h->id || (h->cmd != BR_TRANSACTION && h->cmd != BR_REPLY) ||
            h->data_size > INLINE_MAX || h->offsets_size > INLINE_MAX) return bad(l);
        size_t aligned = ((size_t)h->data_size + 7) & ~(size_t)7;
        if (aligned > size - h->br_size || h->offsets_size != size - h->br_size - aligned) return bad(l);
    } else if (h->id || h->data_size || h->offsets_size || h->br_size != size) return bad(l);
    const unsigned char *br = payload;
    unsigned char acks[4096]; size_t n = 0;
    for (size_t i = 0; i < h->br_size;) {
        uint32_t cmd = read32(br + i); i += 4;
        if (cmd == BR_TRANSACTION_COMPLETE) continue;
        if (cmd != BR_INCREFS && cmd != BR_ACQUIRE && cmd != BR_RELEASE && cmd != BR_DECREFS) return bad(l);
        if (h->br_size - i < 16 || read64(br + i) != l->ptr || read64(br + i + 8) != l->cookie) return bad(l);
        if (cmd == BR_INCREFS || cmd == BR_ACQUIRE) {
            put32(acks + n, cmd == BR_INCREFS ? BC_INCREFS_DONE : BC_ACQUIRE_DONE);
            put64(acks + n + 4, l->ptr); put64(acks + n + 12, l->cookie); n += 20;
        }
        i += 16;
    }
    if (n && send_commands(l, acks, n) != 0) return -1;
    if (h->flags & F_TXN) l->pending_id = h->id;
    return 0;
}

int rme_vib_lifecycle_free(struct rme_vib_lifecycle *l) {
    if (!l || !l->pending_id) { errno = EINVAL; return -1; }
    unsigned char cmd[12]; put32(cmd, BC_AEMU_FREE); put64(cmd + 4, l->pending_id);
    if (send_commands(l, cmd, sizeof cmd) != 0) return -1;
    l->pending_id = 0; return 0;
}

int rme_vib_lifecycle_exit(struct rme_vib_lifecycle *l) {
    if (!l || !l->entered || l->pending_id) { errno = EINVAL; return -1; }
    uint32_t cmd = BC_EXIT_LOOPER;
    if (send_commands(l, &cmd, sizeof cmd) != 0) return -1;
    l->entered = 0; return 0;
}
