#include "radio_host_gate.h"
#include "../gueststubs/radio-bridge/gate.h"
#include <stddef.h>

int rme_host_read_ack(int socket_fd, int pidfd, int32_t pid, uint32_t uid,
                      uint64_t epoch, uint64_t sequence) {
    struct rme_gate gate;
    if (socket_fd < 0 || pidfd < 0 || sequence == 0 ||
        rme_gate_init(&gate, pid, uid, epoch) != 0) return -1;
    gate.last_sequence = sequence - 1;
    uint8_t frame[279]; size_t size = 0;
    int received = rme_gate_receive(&gate, socket_fd, pidfd, frame, sizeof(frame), &size);
    if (received <= 0) return received;
    if (size != 24 || frame[20] != 3 || frame[21] != 0 || frame[22] != 1 ||
        frame[23] < 1 || frame[23] > 3) return -1;
    return frame[23];
}
