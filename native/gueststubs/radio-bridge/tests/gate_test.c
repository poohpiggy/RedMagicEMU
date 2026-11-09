#include "gate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static void put64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; }
}
static void frame(uint8_t *p, uint64_t epoch, uint64_t seq) {
    memset(p, 0, 24); memcpy(p, "RME1", 4);
    put64(p + 4, epoch); put64(p + 12, seq);
    p[20] = 1; p[21] = 0; p[22] = 1; p[23] = 42;
}

int main(void) {
    struct rme_gate g; struct rme_credential good = { 123, 456 }, wrong = { 124, 456 };
    uint8_t msg[24]; frame(msg, 77, 1);
    assert(rme_gate_init(&g, 123, 456, 77) == 0);
    assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, NULL, 0));
    assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &wrong, 1));
    struct rme_credential duplicate[2] = { good, good };
    assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, duplicate, 2));
    assert(!rme_gate_accept(&g, msg, sizeof(msg), MSG_CTRUNC, &good, 1));
    assert(!rme_gate_accept(&g, msg, sizeof(msg), MSG_TRUNC, &good, 1));
    assert(!rme_gate_accept(&g, msg, sizeof(msg) - 1, 0, &good, 1));
    msg[0] = 'X'; assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1)); msg[0] = 'R';
    msg[20] = 3; assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1)); msg[20] = 1;
    put64(msg + 4, 78); assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1)); put64(msg + 4, 77);
    put64(msg + 12, 2); assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1)); put64(msg + 12, 1);
    assert(g.last_sequence == 0);
    assert(rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1));
    assert(g.last_sequence == 1);
    assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1));
    frame(msg, 77, 2); assert(rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1));
    assert(g.last_sequence == 2);
    frame(msg, 77, 3); msg[20] = 3; msg[23] = 9;
    assert(!rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1) && g.last_sequence == 2);
    msg[23] = 1;
    assert(rme_gate_accept(&g, msg, sizeof(msg), 0, &good, 1) && g.last_sequence == 3);
    /* Host hangup identifies the original incoming frame, not a call index. */
    uint8_t hangup[39] = {0}; memcpy(hangup, "RME1", 4);
    put64(hangup + 4, 77); put64(hangup + 12, 4);
    hangup[20] = 4; hangup[22] = 16;
    put64(hangup + 23, 77); put64(hangup + 31, 1);
    assert(!rme_gate_accept(&g, hangup, sizeof(hangup) - 1, 0, &good, 1));
    hangup[22] = 15; assert(!rme_gate_accept(&g, hangup, sizeof(hangup) - 1, 0, &good, 1)); hangup[22] = 16;
    uint8_t long_hangup[40]; memcpy(long_hangup,hangup,sizeof(hangup));
    long_hangup[22] = 17; long_hangup[39] = 0;
    assert(!rme_gate_accept(&g,long_hangup,sizeof(long_hangup),0,&good,1));
    put64(hangup + 23, 0); assert(!rme_gate_accept(&g, hangup, sizeof(hangup), 0, &good, 1));
    put64(hangup + 23, 78); assert(!rme_gate_accept(&g, hangup, sizeof(hangup), 0, &good, 1));
    put64(hangup + 23, 77); put64(hangup + 31, 0);
    assert(!rme_gate_accept(&g, hangup, sizeof(hangup), 0, &good, 1));
    put64(hangup + 31, 1);
    assert(!rme_gate_accept(&g, hangup, sizeof(hangup), 0, &wrong, 1));
    assert(g.last_sequence == 3);
    assert(rme_gate_accept(&g, hangup, sizeof(hangup), 0, &good, 1) && g.last_sequence == 4);
    assert(!rme_gate_accept(&g, hangup, sizeof(hangup), 0, &good, 1));
    puts("RADIO_BRIDGE_GATE_OK"); return 0;
}
