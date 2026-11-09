#ifndef RME_RADIO_HOST_GATE_H
#define RME_RADIO_HOST_GATE_H
#include <stdint.h>
/* 0 no packet, -1 reject/closed, 1..3 authenticated ACK status. */
int rme_host_read_ack(int socket_fd, int pidfd, int32_t pid, uint32_t uid,
                      uint64_t epoch, uint64_t sequence);
#endif
