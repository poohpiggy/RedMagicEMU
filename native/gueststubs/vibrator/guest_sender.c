#include "guest_sender.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int rme_guest_vibration_init(struct rme_guest_vibration_sender *sender, int fd, uint64_t epoch) {
    if (!sender || fd < 0 || epoch == 0 || epoch > INT64_MAX) return -EINVAL;
    int type = 0;
    socklen_t length = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0) return -errno;
    if (length != sizeof(type) || type != SOCK_SEQPACKET) return -EPROTOTYPE;
    struct sockaddr_un local = {0}, remote = {0};
    length = sizeof(local);
    if (getsockname(fd, (struct sockaddr *)&local, &length) != 0) return -errno;
    if (local.sun_family != AF_UNIX) return -EAFNOSUPPORT;
    length = sizeof(remote);
    if (getpeername(fd, (struct sockaddr *)&remote, &length) != 0) return -errno;
    if (remote.sun_family != AF_UNIX) return -EAFNOSUPPORT;
    int error = pthread_mutex_init(&sender->lock, NULL);
    if (error) return -error;
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        error = errno;
        pthread_mutex_destroy(&sender->lock);
        return -error;
    }
    sender->fd = fd;
    sender->close_error = 0;
    sender->epoch = epoch;
    sender->next_sequence = 1;
    return 0;
}

static void put64(unsigned char *output, uint64_t value) {
    for (int i = 7; i >= 0; --i) { output[i] = (unsigned char)value; value >>= 8; }
}

/* Once a send has failed, the owner must never reuse the session. */
static void retire(struct rme_guest_vibration_sender *sender) {
    int fd = sender->fd;
    sender->fd = -1;
    if (fd >= 0 && close(fd) != 0) sender->close_error = -errno;
}

static int send_locked(struct rme_guest_vibration_sender *sender, unsigned char op, uint32_t ms) {
    if (sender->fd < 0) return -ESHUTDOWN;
    if (sender->next_sequence == 0 || sender->next_sequence >= INT64_MAX) {
        retire(sender);
        return -EOVERFLOW;
    }
    unsigned char frame[28] = {0};
    memcpy(frame, "RME1", 4);
    put64(frame + 4, sender->epoch);
    put64(frame + 12, sender->next_sequence);
    frame[20] = 1;
    frame[22] = 5;
    frame[23] = op;
    frame[24] = (unsigned char)(ms >> 24);
    frame[25] = (unsigned char)(ms >> 16);
    frame[26] = (unsigned char)(ms >> 8);
    frame[27] = (unsigned char)ms;
    ssize_t count = send(sender->fd, frame, sizeof(frame), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count != (ssize_t)sizeof(frame)) {
        int error = count < 0 ? errno : EIO;
        retire(sender);
        return -error;
    }
    ++sender->next_sequence;
    return 0;
}

int rme_guest_vibration_on(struct rme_guest_vibration_sender *sender, uint32_t milliseconds) {
    if (!sender) return -EINVAL;
    int error = pthread_mutex_lock(&sender->lock);
    if (error) return -error;
    int result = sender->fd < 0 ? -ESHUTDOWN :
                 (milliseconds == 0 || milliseconds > 5000) ? -EINVAL :
                 send_locked(sender, 1, milliseconds);
    pthread_mutex_unlock(&sender->lock);
    return result;
}

int rme_guest_vibration_off(struct rme_guest_vibration_sender *sender) {
    if (!sender) return -EINVAL;
    int error = pthread_mutex_lock(&sender->lock);
    if (error) return -error;
    int result = send_locked(sender, 2, 0);
    pthread_mutex_unlock(&sender->lock);
    return result;
}

int rme_guest_vibration_close(struct rme_guest_vibration_sender *sender) {
    if (!sender) return -EINVAL;
    int error = pthread_mutex_lock(&sender->lock);
    if (error) return -error;
    if (sender->fd >= 0) retire(sender);
    int result = sender->close_error;
    pthread_mutex_unlock(&sender->lock);
    return result;
}

int rme_guest_vibration_destroy(struct rme_guest_vibration_sender *sender) {
    if (!sender) return -EINVAL;
    int error = pthread_mutex_lock(&sender->lock);
    if (error) return -error;
    int active = sender->fd >= 0;
    int result = sender->close_error;
    pthread_mutex_unlock(&sender->lock);
    if (active) return -EBUSY;
    error = pthread_mutex_destroy(&sender->lock);
    return result ? result : -error;
}
