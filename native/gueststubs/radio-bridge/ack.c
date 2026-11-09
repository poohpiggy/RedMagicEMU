#include "ack.h"
#include <stddef.h>
#include <stdint.h>

int rme_ack_init(struct rme_ack_tracker *t, uint64_t epoch) {
    if (!t || !epoch) return 0;
    t->epoch = epoch; t->sequence = 0; t->deadline_ns = 0; t->state = RME_ACK_IDLE; return 1;
}
int rme_ack_begin(struct rme_ack_tracker *t, uint64_t now, uint64_t timeout, uint64_t *sequence) {
    if (!t || !sequence || !t->epoch || !timeout || now > UINT64_MAX - timeout ||
            t->sequence == UINT64_MAX || t->state == RME_ACK_PENDING || t->state == RME_ACK_UNKNOWN) return 0;
    t->sequence++; t->deadline_ns = now + timeout; t->state = RME_ACK_PENDING; *sequence = t->sequence; return 1;
}
int rme_ack_expire(struct rme_ack_tracker *t, uint64_t now) {
    if (!t || t->state != RME_ACK_PENDING || now < t->deadline_ns) return 0;
    t->state = RME_ACK_UNKNOWN; return 1;
}
int rme_ack_accept(struct rme_ack_tracker *t, uint64_t epoch, uint64_t sequence, int result, uint64_t now) {
    if (!t || t->state != RME_ACK_PENDING) return 0;
    if (rme_ack_expire(t, now)) return 0;
    if (epoch != t->epoch || sequence != t->sequence ||
            (result != RME_ACK_SUBMITTED && result != RME_ACK_REJECTED && result != RME_ACK_UNCERTAIN)) return 0;
    t->state = (enum rme_ack_state)result; return 1;
}
