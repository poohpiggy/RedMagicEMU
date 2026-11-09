#include "../vibration_bproto_lifecycle.h"
#include "../bproto.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BR_INCREFS 0x80107207u
#define BR_ACQUIRE 0x80107208u
#define BR_RELEASE 0x80107209u
#define BR_DECREFS 0x8010720au
#define BR_TRANSACTION_COMPLETE 0x00007206u
#define BR_TRANSACTION 0x80407202u
#define BC_INCREFS_DONE 0x40106308u
#define BC_ACQUIRE_DONE 0x40106309u
#define BC_ENTER_LOOPER 0x0000630cu
#define BC_EXIT_LOOPER 0x0000630du

static void put32(unsigned char *p, uint32_t n) { memcpy(p, &n, sizeof n); }
static void put64(unsigned char *p, uint64_t n) { memcpy(p, &n, sizeof n); }
static uint32_t get32(const unsigned char *p) { uint32_t n; memcpy(&n, p, sizeof n); return n; }
static uint64_t get64(const unsigned char *p) { uint64_t n; memcpy(&n, p, sizeof n); return n; }
static size_t event(unsigned char *p, uint32_t cmd, uint64_t ptr, uint64_t cookie) {
    put32(p, cmd); put64(p + 4, ptr); put64(p + 12, cookie); return 20;
}
static void pair(struct rme_bproto_client *c, int *peer) {
    int fd[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) == 0);
    c->fd = fd[0]; c->guest_uid = 1000; *peer = fd[1];
}
static ssize_t packet(int peer, unsigned char *buf, size_t cap) { return recv(peer, buf, cap, 0); }

int main(void) {
    struct rme_bproto_client c; struct rme_vib_lifecycle l; int peer; unsigned char out[256];
    const uint64_t ptr = 0x12345678, cookie = 0x55667788;
    pair(&c, &peer); rme_vib_lifecycle_init(&l, &c, ptr, cookie);
    assert(rme_vib_lifecycle_enter(&l) == 0);
    assert(packet(peer, out, sizeof out) == (ssize_t)(sizeof(struct c_hdr) + 4));
    assert(get32(out) == OP_BCS && get32(out + 12) == 1000 && get32(out + 16) == BC_ENTER_LOOPER);
    unsigned char br[100]; size_t n = 0;
    n += event(br + n, BR_INCREFS, ptr, cookie);
    n += event(br + n, BR_ACQUIRE, ptr, cookie);
    put32(br + n, BR_TRANSACTION_COMPLETE); n += 4;
    n += event(br + n, BR_RELEASE, ptr, cookie);
    n += event(br + n, BR_DECREFS, ptr, cookie);
    struct d_hdr hdr = { .br_size = (unsigned)n };
    assert(rme_vib_lifecycle_observe(&l, &hdr, br, n) == 0);
    assert(packet(peer, out, sizeof out) == (ssize_t)(sizeof(struct c_hdr) + 40));
    assert(get32(out) == OP_BCS && get32(out + 16) == BC_INCREFS_DONE);
    assert(get64(out + 20) == ptr && get64(out + 28) == cookie);
    assert(get32(out + 36) == BC_ACQUIRE_DONE && get64(out + 40) == ptr && get64(out + 48) == cookie);
    hdr = (struct d_hdr){ .flags = F_TXN, .cmd = BR_TRANSACTION, .id = 0x1234 };
    assert(rme_vib_lifecycle_observe(&l, &hdr, NULL, 0) == 0);
    assert(rme_vib_lifecycle_free(&l) == 0);
    assert(packet(peer, out, sizeof out) == (ssize_t)(sizeof(struct c_hdr) + 12));
    assert(get32(out + 16) == BC_AEMU_FREE && get64(out + 20) == 0x1234);
    assert(rme_vib_lifecycle_free(&l) == -1);
    assert(rme_vib_lifecycle_exit(&l) == 0);
    assert(packet(peer, out, sizeof out) == (ssize_t)(sizeof(struct c_hdr) + 4));
    assert(get32(out + 16) == BC_EXIT_LOOPER);
    rme_bproto_close(&c); close(peer);

    pair(&c, &peer); rme_vib_lifecycle_init(&l, &c, ptr, cookie);
    n = event(br, BR_ACQUIRE, ptr, cookie + 1);
    hdr = (struct d_hdr){ .br_size = (unsigned)n };
    assert(rme_vib_lifecycle_observe(&l, &hdr, br, n) == -1 && c.fd == -1);
    assert(recv(peer, out, sizeof out, 0) == 0); close(peer);

    pair(&c, &peer); rme_vib_lifecycle_init(&l, &c, ptr, cookie);
    n = event(br, BR_INCREFS, ptr, cookie); put32(br + n, BR_ACQUIRE); n += 4;
    hdr = (struct d_hdr){ .br_size = (unsigned)n };
    assert(rme_vib_lifecycle_observe(&l, &hdr, br, n) == -1 && c.fd == -1);
    assert(recv(peer, out, sizeof out, 0) == 0); close(peer);

    pair(&c, &peer); rme_vib_lifecycle_init(&l, &c, ptr, cookie);
    hdr = (struct d_hdr){ .flags = F_TXN, .cmd = BR_TRANSACTION, .id = 123 };
    assert(rme_vib_lifecycle_observe(&l, &hdr, NULL, 0) == 0);
    assert(rme_vib_lifecycle_observe(&l, &hdr, NULL, 0) == -1 && c.fd == -1);
    close(peer);

    pair(&c, &peer); rme_vib_lifecycle_init(&l, &c, ptr, cookie);
    hdr = (struct d_hdr){ .flags = F_WAKE | F_AGAIN };
    assert(rme_vib_lifecycle_observe(&l, &hdr, NULL, 0) == -1 && c.fd == -1);
    close(peer);
    puts("VIBRATION_BPROTO_LIFECYCLE_OK");
}
