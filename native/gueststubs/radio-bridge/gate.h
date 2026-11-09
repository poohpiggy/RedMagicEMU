#ifndef RME_RADIO_BRIDGE_GATE_H
#define RME_RADIO_BRIDGE_GATE_H

#include <stddef.h>
#include <stdint.h>

/* Input metadata must come from the receiving host kernel, not the Guest. */
struct rme_credential { int32_t pid; uint32_t uid; };
struct rme_gate { int32_t pid; uint32_t uid; uint64_t epoch, last_sequence; };

int rme_gate_init(struct rme_gate *g, int32_t pid, uint32_t uid, uint64_t epoch);
/* Accepts one framed message. On every rejection the sequence remains unchanged. */
int rme_gate_accept(struct rme_gate *g, const uint8_t *frame, size_t length,
                    int message_flags, const struct rme_credential *credentials,
                    size_t credential_count);

/* Connected AF_UNIX SEQPACKET socket, SO_PASSCRED enabled, and a pidfd obtained
 * by the authenticated launcher for g->pid are required. Does not block.
 * 1 = accepted, 0 = no packet, -1 = rejected or unavailable. */
int rme_gate_receive(struct rme_gate *g, int socket_fd, int pidfd,
                     uint8_t *output, size_t capacity, size_t *output_length);

#endif
