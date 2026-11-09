#define _POSIX_C_SOURCE 200809L
#include "vibration_bproto_service_fixture.h"
#include "../vibration_bproto_lifecycle.h"
#include "../vibration_binder_parcel.h"
#include "../vibration_binder_reply.h"
#include "../bproto.h"
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define BR_TRANSACTION 0x80407202u
#define BR_REPLY 0x80407203u

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* This fixture intentionally does not implement nested/concurrent Binder
 * transactions, caller identity, registration lookup or exception replies.
 * Any unknown message closes the connection and cancels the fake actuator. */
int rme_test_vib_service(struct rme_bproto_client *client,
                         const struct rme_vib_add_service_input *registration,
                         const struct rme_vib_ops *ops, int stop_fd) {
    int result = -1, registered = 0;
    struct rme_vib_lifecycle life;
    rme_vib_lifecycle_init(&life, client, registration ? registration->binder_ptr : 0,
                           registration ? registration->binder_cookie : 0);
    if (!client || client->fd < 0 || !registration || !ops || !ops->on || !ops->off || stop_fd < 0 || stop_fd == client->fd) goto done;
    int64_t now = monotonic_ms();
    if (now < 0) goto done;
    const int64_t deadline = now + 3000;
    unsigned char registration_parcel[200]; size_t size = 0; uint64_t candidate_offset = 0;
    if (rme_vib_encode_add_service(registration, registration_parcel, sizeof registration_parcel,
                                   &size, &candidate_offset) ||
        rme_vib_lifecycle_enter(&life) ||
        rme_bproto_txn(client, 0, 5, 0, registration_parcel, size, &candidate_offset, 1)) goto done;
    for (;;) {
        if (rme_bproto_read_request(client, 4096)) goto done;
        struct pollfd poll_fds[] = {{client->fd, POLLIN, 0}, {stop_fd, POLLIN, 0}};
        int n;
        do {
            int timeout = -1;
            if (!registered) {
                now = monotonic_ms();
                if (now < 0 || now >= deadline) goto done;
                timeout = (int)(deadline - now);
            }
            n = poll(poll_fds, 2, timeout);
        } while (n < 0 && errno == EINTR);
        if (n <= 0 || (poll_fds[0].revents & (POLLERR | POLLNVAL)) || (poll_fds[1].revents & POLLNVAL)) goto done;
        if (poll_fds[1].revents & (POLLIN | POLLHUP)) { result = registered ? 0 : -1; goto done; }
        if (!(poll_fds[0].revents & (POLLIN | POLLHUP))) goto done;
        struct d_hdr h; unsigned char payload[4096]; size_t length = 0;
        if (rme_bproto_receive(client, &h, payload, sizeof payload, &length) ||
            rme_vib_lifecycle_observe(&life, &h, payload, length)) goto done;
        if (!(h.flags & F_TXN)) continue;
        const unsigned char *data = payload + h.br_size;
        if (!registered) {
            static const unsigned char ok[4] = {0};
            if (h.cmd != BR_REPLY || h.tflags || h.offsets_size || h.data_size != sizeof ok || memcmp(data, ok, sizeof ok)) goto done;
            if (rme_vib_lifecycle_free(&life)) goto done;
            registered = 1;
            continue;
        }
        if (h.cmd != BR_TRANSACTION || h.ptr != registration->binder_ptr || h.cookie != registration->binder_cookie) goto done;
        struct rme_vib_request request;
        if (rme_vib_decode(h.code, h.tflags, data, (size_t)h.data_size,
                           (size_t)h.offsets_size, (size_t)h.extra_size, &request) != RME_VIB_DECODE_OK) goto done;
        struct rme_vib_reply response;
        enum rme_vib_status status = rme_vib_dispatch(&request, ops, &response);
        unsigned char reply[8]; size_t reply_size = 0;
        if (rme_vib_encode_success(h.code, status, &response, reply, sizeof reply, &reply_size) != RME_VIB_ENCODE_OK ||
            rme_bproto_reply(client, reply, reply_size) || rme_vib_lifecycle_free(&life)) goto done;
    }
done:
    if (client && client->fd >= 0 && life.entered && !life.pending_id && rme_vib_lifecycle_exit(&life)) result = -1;
    if (client) rme_bproto_close(client);
    if (ops && ops->off && ops->off(ops->ctx)) result = -1;
    return result;
}
