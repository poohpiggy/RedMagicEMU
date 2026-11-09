#ifndef RME_GUEST_VIBRATION_SENDER_H
#define RME_GUEST_VIBRATION_SENDER_H

#include <pthread.h>
#include <stdint.h>

/* The caller must authenticate the host and supply a dedicated connected
 * AF_UNIX SOCK_SEQPACKET socket. This object does not authenticate a peer,
 * authorize a Guest caller, register a HAL, or report physical vibration. */
struct rme_guest_vibration_sender {
    pthread_mutex_t lock;
    int fd;
    int close_error;
    uint64_t epoch, next_sequence;
};

/* Initialize exactly once. On success takes ownership of fd and sets
 * FD_CLOEXEC; on failure caller retains it. The caller must create fd with
 * SOCK_CLOEXEC when concurrent exec is possible, and must not keep aliases
 * when it expects close to signal EOF. Do not call other methods after destroy.
 * Sender methods are not async-signal-safe and must not run in a signal handler. */
int rme_guest_vibration_init(struct rme_guest_vibration_sender *sender, int fd, uint64_t epoch);
/* 0 means only that a complete frame was queued, not that vibration occurred.
 * Invalid durations do not consume a sequence. Transport failure retires the
 * sender; no automatic retry or replay is performed. */
int rme_guest_vibration_on(struct rme_guest_vibration_sender *sender, uint32_t milliseconds);
int rme_guest_vibration_off(struct rme_guest_vibration_sender *sender);
/* Close is idempotent and serialized with sends. Call destroy only after close,
 * when all threads using the sender have stopped. */
int rme_guest_vibration_close(struct rme_guest_vibration_sender *sender);
int rme_guest_vibration_destroy(struct rme_guest_vibration_sender *sender);

#endif
