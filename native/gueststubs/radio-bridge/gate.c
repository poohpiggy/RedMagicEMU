#include "gate.h"
#include <limits.h>
#include <string.h>
#include <sys/socket.h>

enum { HEADER_SIZE = 23, MAX_PAYLOAD = 256 };

static uint64_t be64(const uint8_t *p) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) result = (result << 8) | p[i];
    return result;
}

int rme_gate_init(struct rme_gate *g, int32_t pid, uint32_t uid, uint64_t epoch) {
    if (!g || pid <= 0 || epoch == 0) return -1;
    g->pid = pid; g->uid = uid; g->epoch = epoch; g->last_sequence = 0;
    return 0;
}

int rme_gate_accept(struct rme_gate *g, const uint8_t *frame, size_t length,
                    int flags, const struct rme_credential *credentials, size_t count) {
    if (!g || !frame || !credentials || count != 1 || (flags & (MSG_TRUNC | MSG_CTRUNC))) return 0;
    if (credentials[0].pid != g->pid || credentials[0].uid != g->uid) return 0;
    if (length < HEADER_SIZE || length > HEADER_SIZE + MAX_PAYLOAD || memcmp(frame, "RME1", 4)) return 0;
    size_t payload = ((size_t)frame[21] << 8) | frame[22];
    if (!payload || payload > MAX_PAYLOAD || length != HEADER_SIZE + payload) return 0;
    if (frame[20] != 1 && frame[20] != 2 && frame[20] != 3 && frame[20] != 4) return 0;
    if (frame[20] == 3 && (payload != 1 || frame[HEADER_SIZE] < 1 || frame[HEADER_SIZE] > 3)) return 0;
    /* A hangup names an incoming frame from this same authenticated session. */
    if (frame[20] == 4 && (payload != 16 || be64(frame + HEADER_SIZE) != g->epoch ||
                           be64(frame + HEADER_SIZE + 8) == 0)) return 0;
    if (be64(frame + 4) != g->epoch || g->last_sequence == UINT64_MAX ||
            be64(frame + 12) != g->last_sequence + 1) return 0;
    ++g->last_sequence;
    return 1;
}
