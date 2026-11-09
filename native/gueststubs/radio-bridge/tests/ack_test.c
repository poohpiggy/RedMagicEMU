#include "ack.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    struct rme_ack_tracker tracker; uint64_t seq = 0;
    assert(rme_ack_init(&tracker, 11));
    assert(rme_ack_begin(&tracker, 100, 50, &seq) && seq == 1);
    assert(!rme_ack_begin(&tracker, 101, 50, &seq));
    assert(!rme_ack_accept(&tracker, 12, 1, RME_ACK_SUBMITTED, 102));
    assert(!rme_ack_accept(&tracker, 11, 2, RME_ACK_SUBMITTED, 103));
    assert(!rme_ack_accept(&tracker, 11, 1, 99, 104));
    assert(tracker.state == RME_ACK_PENDING);
    assert(rme_ack_accept(&tracker, 11, 1, RME_ACK_SUBMITTED, 105));
    assert(tracker.state == RME_ACK_SUBMITTED);
    assert(!rme_ack_accept(&tracker, 11, 1, RME_ACK_SUBMITTED, 106));
    assert(rme_ack_begin(&tracker, 200, 50, &seq) && seq == 2);
    assert(rme_ack_accept(&tracker, 11, 2, RME_ACK_UNCERTAIN, 201));
    assert(tracker.state == RME_ACK_UNCERTAIN);
    assert(rme_ack_begin(&tracker, 300, 50, &seq) && seq == 3);
    assert(rme_ack_accept(&tracker, 11, 3, RME_ACK_REJECTED, 301));
    assert(tracker.state == RME_ACK_REJECTED);
    assert(rme_ack_begin(&tracker, 400, 50, &seq) && seq == 4);
    assert(!rme_ack_expire(&tracker, 449));
    assert(rme_ack_expire(&tracker, 450) && tracker.state == RME_ACK_UNKNOWN);
    assert(!rme_ack_accept(&tracker, 11, 4, RME_ACK_SUBMITTED, 451));
    assert(!rme_ack_begin(&tracker, 500, 50, &seq));
    assert(rme_ack_init(&tracker, 12));
    assert(rme_ack_begin(&tracker, 600, 50, &seq) && seq == 1);
    assert(!rme_ack_accept(&tracker, 11, 1, RME_ACK_SUBMITTED, 601));
    assert(rme_ack_accept(&tracker, 12, 1, RME_ACK_SUBMITTED, 602));
    puts("RADIO_BRIDGE_ACK_OK"); return 0;
}
