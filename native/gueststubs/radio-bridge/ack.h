#ifndef RME_RADIO_BRIDGE_ACK_H
#define RME_RADIO_BRIDGE_ACK_H
#include <stdint.h>

enum rme_ack_state {
    RME_ACK_IDLE = 0,
    RME_ACK_SUBMITTED = 1,
    RME_ACK_REJECTED = 2,
    RME_ACK_UNCERTAIN = 3,
    RME_ACK_PENDING = 4,
    RME_ACK_UNKNOWN = 5
};
struct rme_ack_tracker { uint64_t epoch, sequence, deadline_ns; enum rme_ack_state state; };
int rme_ack_init(struct rme_ack_tracker *tracker, uint64_t epoch);
int rme_ack_begin(struct rme_ack_tracker *tracker, uint64_t now_ns, uint64_t timeout_ns, uint64_t *sequence);
int rme_ack_accept(struct rme_ack_tracker *tracker, uint64_t epoch, uint64_t sequence, int result, uint64_t now_ns);
int rme_ack_expire(struct rme_ack_tracker *tracker, uint64_t now_ns);
struct rme_gate;
/* 1 = matching ACK, 0 = no packet, -1 = rejected packet, -2 = unknown/retire session. */
int rme_ack_receive(struct rme_ack_tracker *tracker, struct rme_gate *gate, int socket_fd, int pidfd);
#endif
