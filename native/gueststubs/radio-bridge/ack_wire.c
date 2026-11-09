#include "ack.h"
#include "gate.h"
#include <stdint.h>
#include <time.h>

static int monotonic_ns(uint64_t *out) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0 ||
            (uint64_t)now.tv_sec > (UINT64_MAX - (uint64_t)now.tv_nsec) / 1000000000ULL) return 0;
    *out = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
    return 1;
}
static uint64_t be64(const uint8_t *p) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}
int rme_ack_receive(struct rme_ack_tracker *tracker, struct rme_gate *gate, int fd, int pidfd) {
    if (!tracker || !gate || tracker->state != RME_ACK_PENDING || tracker->epoch != gate->epoch) return -2;
    uint64_t now;
    if (!monotonic_ns(&now)) { tracker->state = RME_ACK_UNKNOWN; return -2; }
    if (rme_ack_expire(tracker, now)) return -2;
    uint8_t frame[279]; size_t length = 0;
    int result = rme_gate_receive(gate, fd, pidfd, frame, sizeof(frame), &length);
    if (!monotonic_ns(&now)) { tracker->state = RME_ACK_UNKNOWN; return -2; }
    if (rme_ack_expire(tracker, now)) return -2;
    if (result <= 0) return result;
    if (length != 24 || frame[20] != 3 ||
            !rme_ack_accept(tracker, be64(frame + 4), be64(frame + 12), frame[23], now)) {
        tracker->state = RME_ACK_UNKNOWN;
        return -2;
    }
    return 1;
}
