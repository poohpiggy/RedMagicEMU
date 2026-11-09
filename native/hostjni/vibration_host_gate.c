#include "vibration_host_gate.h"
#include "../gueststubs/radio-bridge/gate.h"
#include <limits.h>
#include <stddef.h>

int rme_host_read_vibration(int socket_fd, int pidfd, int32_t pid, uint32_t uid,
                            uint64_t epoch, uint64_t sequence) {
    struct rme_gate gate;
    if (socket_fd < 0 || pidfd < 0 || sequence == 0 || sequence >= INT64_MAX ||
        rme_gate_init(&gate, pid, uid, epoch) != 0) return -1;
    gate.last_sequence = sequence - 1;
    uint8_t frame[28]; size_t length = 0;
    int received = rme_gate_receive(&gate, socket_fd, pidfd, frame, sizeof(frame), &length);
    if (received <= 0) return received;
    if (length != sizeof(frame) || frame[20] != 1 || frame[21] != 0 || frame[22] != 5) return -1;
    uint32_t duration = ((uint32_t)frame[24] << 24) | ((uint32_t)frame[25] << 16) |
                        ((uint32_t)frame[26] << 8) | frame[27];
    if (frame[23] == 2 && duration == 0) return 1;
    if (frame[23] == 1 && duration >= 1 && duration <= 5000) return (int)duration + 1;
    return -1;
}
