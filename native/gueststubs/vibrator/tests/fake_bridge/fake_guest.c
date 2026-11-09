#include "fake_guest.h"
#include <limits.h>
#include <string.h>
#include <sys/socket.h>

static void put64(unsigned char *out, uint64_t value) {
    for (int i = 7; i >= 0; --i) { out[i] = (unsigned char)value; value >>= 8; }
}

static int send_command(struct fake_guest *guest, unsigned char op, uint32_t duration) {
    if (!guest || guest->fd < 0 || guest->stopped || !guest->epoch ||
        !guest->sequence || guest->sequence >= INT64_MAX) return -1;
    unsigned char frame[28] = {0};
    memcpy(frame, "RME1", 4);
    put64(frame + 4, guest->epoch);
    put64(frame + 12, guest->sequence);
    frame[20] = 1; frame[22] = 5; frame[23] = op;
    frame[24] = (unsigned char)(duration >> 24);
    frame[25] = (unsigned char)(duration >> 16);
    frame[26] = (unsigned char)(duration >> 8);
    frame[27] = (unsigned char)duration;
    if (send(guest->fd, frame, sizeof frame, MSG_NOSIGNAL) != sizeof frame) return -1;
    ++guest->sequence;
    return 0;
}

int fake_guest_on(struct fake_guest *guest, uint32_t duration_ms) {
    if (duration_ms == 0 || duration_ms > 5000) return -1;
    return send_command(guest, 1, duration_ms);
}

int fake_guest_off(struct fake_guest *guest) { return send_command(guest, 2, 0); }
void fake_guest_stop(struct fake_guest *guest) { if (guest) guest->stopped = 1; }
