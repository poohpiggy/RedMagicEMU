#define _GNU_SOURCE
#include "ack.h"
#include "gate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static void put64(uint8_t *p, uint64_t v) { for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; } }
static void frame(uint8_t *p, uint64_t epoch, uint64_t seq, int kind, int result) {
    memset(p, 0, 24); memcpy(p, "RME1", 4); put64(p + 4, epoch); put64(p + 12, seq);
    p[20] = (uint8_t)kind; p[22] = 1; p[23] = (uint8_t)result;
}
static uint64_t now_ns(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
int main(void) {
    alarm(10); int fd[2], yes = 1;
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fd));
    assert(!setsockopt(fd[0], SOL_SOCKET, SO_PASSCRED, &yes, sizeof(yes)));
    int pidfd = (int)syscall(__NR_pidfd_open, getpid(), 0);
    if (pidfd < 0) { puts("RADIO_BRIDGE_PIDFD_UNAVAILABLE"); return 77; }
    struct rme_gate gate; struct rme_ack_tracker tracker; uint64_t seq;
    assert(!rme_gate_init(&gate, getpid(), getuid(), 99) && rme_ack_init(&tracker, 99));
    assert(rme_ack_begin(&tracker, now_ns(), 5000000000ULL, &seq) && seq == 1);
    uint8_t msg[24]; frame(msg, 99, 1, 3, RME_ACK_SUBMITTED);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_ack_receive(&tracker, &gate, fd[0], pidfd) == 1 && tracker.state == RME_ACK_SUBMITTED);
    assert(rme_ack_begin(&tracker, now_ns(), 5000000000ULL, &seq) && seq == 2);
    frame(msg, 99, 2, 3, RME_ACK_UNCERTAIN); assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_ack_receive(&tracker, &gate, fd[0], pidfd) == 1 && tracker.state == RME_ACK_UNCERTAIN);
    assert(rme_ack_begin(&tracker, now_ns(), 5000000000ULL, &seq) && seq == 3);
    frame(msg, 99, 3, 3, RME_ACK_REJECTED); assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_ack_receive(&tracker, &gate, fd[0], pidfd) == 1 && tracker.state == RME_ACK_REJECTED);
    assert(rme_ack_begin(&tracker, 0, 1, &seq) && seq == 4);
    frame(msg, 99, 4, 3, RME_ACK_SUBMITTED); assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_ack_receive(&tracker, &gate, fd[0], pidfd) == -2 && tracker.state == RME_ACK_UNKNOWN);
    assert(!rme_ack_begin(&tracker, now_ns(), 5000000000ULL, &seq));
    close(fd[0]); close(fd[1]); close(pidfd); puts("RADIO_BRIDGE_ACK_WIRE_OK"); return 0;
}
