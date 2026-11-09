#include "../radio_host_gate.h"
#include "../../gueststubs/radio-bridge/gate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int outcome = 1, bad_kind, bad_length, bad_status;
int rme_gate_receive(struct rme_gate *g, int fd, int pidfd, uint8_t *out,
                     size_t capacity, size_t *length) {
    assert(g->pid == 123 && g->uid == 456 && g->epoch == 78 && g->last_sequence == 4);
    assert(fd == 3 && pidfd == 4 && capacity >= 24);
    if (outcome <= 0) return outcome;
    memset(out, 0, 24); memcpy(out, "RME1", 4);
    out[20] = bad_kind ? 1 : 3; out[22] = 1; out[23] = bad_status ? 9 : 2;
    *length = bad_length ? 23 : 24; return 1;
}
int main(void) {
    assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == 2);
    outcome = 0; assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == 0);
    outcome = -1; assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == -1);
    outcome = 1; bad_kind = 1; assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == -1); bad_kind = 0;
    bad_length = 1; assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == -1); bad_length = 0;
    bad_status = 1; assert(rme_host_read_ack(3, 4, 123, 456, 78, 5) == -1);
    assert(rme_host_read_ack(3, 4, 123, 456, 78, 0) == -1);
    puts("RADIO_HOST_GATE_OK");
}
