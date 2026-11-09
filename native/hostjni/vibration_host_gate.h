#ifndef RME_VIBRATION_HOST_GATE_H
#define RME_VIBRATION_HOST_GATE_H

#include <stdint.h>

/* A dedicated authenticated SEQPACKET connection is required. Return 0 for no
 * packet, -1 for any rejection/closed peer, 1 for cancel, or milliseconds+1
 * for a 1..5000 ms pulse. The caller must retire the channel on -1. Neither
 * authentication of the service process nor this parser establishes that the
 * original Guest application passed Android's permission checks. */
int rme_host_read_vibration(int socket_fd, int pidfd, int32_t pid, uint32_t uid,
                            uint64_t epoch, uint64_t sequence);

#endif
