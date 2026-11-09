#define _GNU_SOURCE
#include "vibration_bproto_step.h"
#include "vibration_binder_parcel.h"
#include "vibration_binder_reply.h"
#include "bproto.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BR_TRANSACTION 0x80407202u
#define BR_REPLY 0x80407203u
#define INTERFACE_TRANSACTION 0x5f4e5446u
#define EXTENSION_TRANSACTION 0x5f455854u

static int64_t now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int fail_locked(struct rme_vib_step *s, int error) {
    s->stopping = 1;
    rme_bproto_close(s->client);
    errno = error;
    return -1;
}
static int stopped_locked(struct rme_vib_step *s) {
    rme_bproto_close(s->client);
    return RME_VIB_STEP_STOPPED;
}
int rme_vib_step_init(struct rme_vib_step *s, struct rme_bproto_client *c,
                      const struct rme_vib_add_service_input *in,
                      int (*peer_alive)(void *), void *peer_ctx) {
    if (!s || !c || c->fd < 0 || !in || !peer_alive) {
        if (c) rme_bproto_close(c);
        errno = EINVAL; return -1;
    }
    memset(s, 0, sizeof *s); s->stop_pipe[0] = s->stop_pipe[1] = -1;
    s->client = c; s->peer_alive = peer_alive; s->peer_ctx = peer_ctx;
    int error = pthread_mutex_init(&s->lock, NULL);
    if (error) { rme_bproto_close(c); errno = error; return -1; }
    s->initialized = 1;
    rme_vib_lifecycle_init(&s->life, c, in->binder_ptr, in->binder_cookie);
    if (!peer_alive(peer_ctx)) { errno = ESRCH; goto fail; }
    int flags = fcntl(c->fd, F_GETFL);
    if (flags < 0 || fcntl(c->fd, F_SETFL, flags | O_NONBLOCK) || pipe2(s->stop_pipe, O_CLOEXEC | O_NONBLOCK)) goto fail;
    int64_t now = now_ms(); if (now < 0) goto fail;
    s->deadline_ms = now + 3000;
    unsigned char parcel[200]; size_t length; uint64_t candidate;
    if (rme_vib_encode_add_service(in, parcel, sizeof parcel, &length, &candidate) ||
        /* The target Guest's addService uses synchronous TF_ACCEPT_FDS.
         * This is a wire capability bit, not permission to receive FDs:
         * the transport still closes any descriptor actually received. */
        rme_vib_lifecycle_enter(&s->life) || rme_bproto_txn(c, 0, 5, 0x10, parcel, length, &candidate, 1)) goto fail;
    return 0;
fail:
    error = errno;
    rme_bproto_close(c);
    if (s->stop_pipe[0] >= 0) close(s->stop_pipe[0]);
    if (s->stop_pipe[1] >= 0) close(s->stop_pipe[1]);
    s->initialized = 0; pthread_mutex_destroy(&s->lock); errno = error; return -1;
}

int rme_vib_step_next(struct rme_vib_step *s, struct rme_vib_step_event *event) {
    if (!s || !event || !s->initialized) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&s->lock);
    if (s->waiting || s->pending_token) { pthread_mutex_unlock(&s->lock); errno = EBUSY; return -1; }
    s->waiting = 1;
    int result = -1;
    for (;;) {
        if (s->stopping) { result = stopped_locked(s); break; }
        if (!s->peer_alive(s->peer_ctx)) { result = fail_locked(s, ESRCH); break; }
        if (rme_bproto_read_request(s->client, 4096)) { result = fail_locked(s, errno); break; }
        struct pollfd fds[] = {{s->client->fd, POLLIN, 0}, {s->stop_pipe[0], POLLIN, 0}};
        int n;
        for (;;) {
            int timeout = -1;
            if (!s->registered) {
                int64_t now = now_ms();
                if (now < 0 || now >= s->deadline_ms) { result = fail_locked(s, ETIMEDOUT); goto done; }
                timeout = (int)(s->deadline_ms - now);
            }
            pthread_mutex_unlock(&s->lock);
            n = poll(fds, 2, timeout);
            int poll_error = errno;
            pthread_mutex_lock(&s->lock);
            if (s->stopping) { result = stopped_locked(s); goto done; }
            if (n < 0 && poll_error == EINTR) continue;
            if (n <= 0) { result = fail_locked(s, n == 0 ? ETIMEDOUT : poll_error); goto done; }
            break;
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) { result = stopped_locked(s); break; }
        if (!(fds[0].revents & (POLLIN | POLLHUP)) || (fds[0].revents & (POLLERR | POLLNVAL))) {
            result = fail_locked(s, EIO); break;
        }
        if (!s->peer_alive(s->peer_ctx)) { result = fail_locked(s, ESRCH); break; }
        struct d_hdr h; unsigned char payload[4096]; size_t length = 0;
        if (rme_bproto_receive(s->client, &h, payload, sizeof payload, &length) ||
            rme_vib_lifecycle_observe(&s->life, &h, payload, length)) { result = fail_locked(s, errno); break; }
        if (!(h.flags & F_TXN)) continue;
        const unsigned char *data = payload + h.br_size;
        if (!s->registered) {
            const unsigned char ok[4] = {0};
            if (h.cmd != BR_REPLY || h.tflags || h.offsets_size || h.data_size != sizeof ok || memcmp(data, ok, sizeof ok) ||
                rme_vib_lifecycle_free(&s->life)) { result = fail_locked(s, EPROTO); break; }
            s->registered = 1;
            *event = (struct rme_vib_step_event){0};
            result = RME_VIB_STEP_REGISTERED;
            break;
        }
        if (h.cmd == BR_TRANSACTION && h.ptr == s->life.ptr && h.cookie == s->life.cookie &&
            h.code == INTERFACE_TRANSACTION && h.tflags == 0x10 && !h.data_size && !h.offsets_size) {
            /* Binder's descriptor query is not an AIDL method: its reply is a
             * String16 directly, without a writeNoException prefix. */
            static const char descriptor[] = "android.hardware.vibrator.IVibrator";
            unsigned char reply[76] = {35, 0, 0, 0};
            for (size_t i = 0; i < sizeof descriptor - 1; ++i) reply[4 + 2*i] = (unsigned char)descriptor[i];
            if (rme_bproto_reply(s->client, reply, sizeof reply) || rme_vib_lifecycle_free(&s->life)) {
                result = fail_locked(s, errno); break;
            }
            continue;
        }
        if (h.cmd == BR_TRANSACTION && h.ptr == s->life.ptr && h.cookie == s->life.cookie &&
            h.code == EXTENSION_TRANSACTION && h.tflags == 0x10 && !h.data_size &&
            !h.offsets_size && !h.extra_size) {
            /* Null strong binder: flat_binder_object then undeclared stability.
             * Null binder objects have no offsets entry in a kernel Parcel. */
            const unsigned char no_extension[28] = {0x85, 0x2a, 0x62, 0x73, 0x13};
            if (rme_bproto_reply(s->client, no_extension, sizeof no_extension) ||
                rme_vib_lifecycle_free(&s->life)) { result = fail_locked(s, errno); break; }
            continue;
        }
        if (h.cmd != BR_TRANSACTION || h.ptr != s->life.ptr || h.cookie != s->life.cookie ||
            rme_vib_decode(h.code, h.tflags, data, (size_t)h.data_size,
                           (size_t)h.offsets_size, (size_t)h.extra_size, &s->pending) != RME_VIB_DECODE_OK ||
            s->serial == UINT64_MAX) { result = fail_locked(s, EPROTO); break; }
        if (s->pending.code == 5 || s->pending.code == 10) {
            /* No predefined effects or composition primitives. */
            struct rme_vib_reply response = { .capabilities = 0 };
            unsigned char empty[8]; size_t written = 0;
            if (rme_vib_encode_success(s->pending.code, RME_VIB_OK, &response, empty, sizeof empty, &written) != RME_VIB_ENCODE_OK ||
                rme_bproto_reply(s->client, empty, written) || rme_vib_lifecycle_free(&s->life)) {
                result = fail_locked(s, EPROTO); break;
            }
            continue;
        }
        if (s->pending.code == 4 || s->pending.code == 8 || s->pending.code == 9 || s->pending.code == 16 || s->pending.code == 17 ||
            s->pending.code == 18 || s->pending.code == 19 || s->pending.code == 20 || s->pending.code == 21 ||
            s->pending.code == 22 || s->pending.code == 23 || s->pending.code == 26 || s->pending.code == 27 ||
            s->pending.code == 28 || s->pending.code == 29) {
            /* AOSP Status::writeToParcel: EX_UNSUPPORTED_OPERATION (-7),
             * empty String16 message, empty remote stack trace header. */
            const unsigned char unsupported[16] = {0xf9, 0xff, 0xff, 0xff};
            if (rme_bproto_reply(s->client, unsupported, sizeof unsupported) || rme_vib_lifecycle_free(&s->life)) {
                result = fail_locked(s, errno); break;
            }
            continue;
        }
        s->pending_token = ++s->serial;
        *event = (struct rme_vib_step_event){s->pending_token, s->pending.code, s->pending.duration_ms};
        result = RME_VIB_STEP_EVENT; break;
    }
done:
    s->waiting = 0;
    pthread_mutex_unlock(&s->lock);
    return result;
}

int rme_vib_step_complete(struct rme_vib_step *s, uint64_t token, int callback_ok) {
    if (!s || !s->initialized) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&s->lock);
    int result = -1;
    if (s->waiting) { errno = EBUSY; goto done; }
    if (s->stopping) { result = stopped_locked(s); goto done; }
    if (!token || token != s->pending_token || callback_ok != 1 || !s->peer_alive(s->peer_ctx)) {
        result = fail_locked(s, EPROTO); goto done;
    }
    struct rme_vib_reply reply = { .capabilities = 0 };
    unsigned char data[8]; size_t length = 0;
    if (rme_vib_encode_success(s->pending.code, RME_VIB_OK, &reply, data, sizeof data, &length) != RME_VIB_ENCODE_OK ||
        rme_bproto_reply(s->client, data, length) || rme_vib_lifecycle_free(&s->life)) {
        result = fail_locked(s, EPROTO); goto done;
    }
    s->pending_token = 0; result = 0;
done:
    pthread_mutex_unlock(&s->lock); return result;
}

int rme_vib_step_stop(struct rme_vib_step *s) {
    if (!s || !s->initialized) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&s->lock);
    int result = 0;
    if (!s->stopping) {
        s->stopping = 1;
        char b = 1; ssize_t n;
        do { n = write(s->stop_pipe[1], &b, 1); } while (n < 0 && errno == EINTR);
        if (n != 1 && !(n < 0 && errno == EAGAIN)) result = -1;
    }
    pthread_mutex_unlock(&s->lock); return result;
}

void rme_vib_step_destroy(struct rme_vib_step *s) {
    if (!s || !s->initialized) return;
    pthread_mutex_lock(&s->lock);
    rme_bproto_close(s->client);
    close(s->stop_pipe[0]); close(s->stop_pipe[1]);
    s->initialized = 0;
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_destroy(&s->lock);
}
