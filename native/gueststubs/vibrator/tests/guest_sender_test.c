#define _GNU_SOURCE
#include "../guest_sender.h"
#include "../../../hostjni/vibration_host_gate.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void ready(int fd) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    assert(poll(&p, 1, 5000) == 1 && (p.revents & POLLIN));
}

static void ack(int fd) { assert(send(fd, "A", 1, MSG_NOSIGNAL) == 1); }
static void await_ack(int fd) { char c; ready(fd); assert(recv(fd, &c, 1, 0) == 1 && c == 'A'); }

static int child_run(const struct sockaddr_un *address, socklen_t length, int finished) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(fd >= 0 && connect(fd, (const struct sockaddr *)address, length) == 0);
    await_ack(fd);
    /* A test-only duplicate reads synchronization bytes; the sender owns fd. */
    int ackfd = dup(fd); assert(ackfd >= 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, fd, 123) == 0);
    assert(rme_guest_vibration_on(&sender, 0) == -EINVAL);
    assert(rme_guest_vibration_on(&sender, 5001) == -EINVAL);
    assert(rme_guest_vibration_on(&sender, 500) == 0); await_ack(ackfd);
    assert(rme_guest_vibration_off(&sender) == 0); await_ack(ackfd);
    assert(rme_guest_vibration_on(&sender, 5000) == 0); await_ack(ackfd);
    assert(rme_guest_vibration_off(&sender) == 0); await_ack(ackfd);
    close(ackfd);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_on(&sender, 500) == -ESHUTDOWN);
    assert(rme_guest_vibration_destroy(&sender) == 0);
    assert(write(finished, "D", 1) == 1);
    char release; assert(read(finished, &release, 1) == 1 && release == 'R');
    close(finished);
    return 0;
}

static void real_cross_process(void) {
    int listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0), syncfd[2];
    assert(listener >= 0 && socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, syncfd) == 0);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    int length = snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1,
                          "rme-guest-vibrator-%ld", (long)getpid());
    assert(length > 0 && (size_t)length < sizeof(address.sun_path) - 1);
    socklen_t address_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + length);
    assert(bind(listener, (const struct sockaddr *)&address, address_len) == 0 && listen(listener, 1) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) { close(listener); close(syncfd[0]); _exit(child_run(&address, address_len, syncfd[1])); }
    close(syncfd[1]); ready(listener);
    int fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC), enabled = 1;
    assert(fd >= 0 && setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled) == 0);
    int pidfd = (int)syscall(SYS_pidfd_open, child, 0);
    if (pidfd < 0 && (errno == ENOSYS || errno == EPERM)) {
        close(fd); close(listener); close(syncfd[0]); waitpid(child, NULL, 0); _exit(77);
    }
    assert(pidfd >= 0); ack(fd);
    const int expected[] = { 501, 1, 5001, 1 };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        ready(fd);
        assert(rme_host_read_vibration(fd, pidfd, child, getuid(), 123, i + 1) == expected[i]);
        ack(fd);
    }
    char done; ready(syncfd[0]); assert(read(syncfd[0], &done, 1) == 1 && done == 'D');
    assert(rme_host_read_vibration(fd, pidfd, child, getuid(), 123, 5) == -1);
    assert(write(syncfd[0], "R", 1) == 1);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(syncfd[0]); close(pidfd); close(fd); close(listener);
}

static void invalid_setup_retains_fd(void) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 1) < 0);
    assert(fcntl(pair[0], F_GETFD) >= 0);
    assert(rme_guest_vibration_init(&sender, pair[0], 0) < 0);
    assert(fcntl(pair[0], F_GETFD) >= 0);
    close(pair[0]); close(pair[1]);
}

static void initialized_descriptor_sets_close_on_exec(void) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
    assert((fcntl(pair[0], F_GETFD) & FD_CLOEXEC) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 11) == 0);
    assert((fcntl(pair[0], F_GETFD) & FD_CLOEXEC) != 0);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_destroy(&sender) == 0);
    close(pair[1]);
}

static void disconnected_retires_sender(void) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 77) == 0);
    close(pair[1]);
    assert(rme_guest_vibration_on(&sender, 1) < 0);
    assert(rme_guest_vibration_off(&sender) == -ESHUTDOWN);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_destroy(&sender) == 0);
}

static void sequence_exhaustion_and_idempotent_close(void) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 88) == 0);
    assert(rme_guest_vibration_destroy(&sender) == -EBUSY);
    sender.next_sequence = INT64_MAX;
    assert(rme_guest_vibration_on(&sender, 1) == -EOVERFLOW);
    assert(rme_guest_vibration_off(&sender) == -ESHUTDOWN);
    int unrelated = open("/dev/null", O_RDONLY | O_CLOEXEC); assert(unrelated >= 0);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(fcntl(unrelated, F_GETFD) >= 0);
    assert(rme_guest_vibration_destroy(&sender) == 0);
    close(unrelated); close(pair[1]);
}

struct thread_args { struct rme_guest_vibration_sender *sender; uint32_t duration; int result; };
static void *concurrent_on(void *opaque) {
    struct thread_args *args = opaque;
    args->result = rme_guest_vibration_on(args->sender, args->duration);
    return NULL;
}

static void concurrent_sends_keep_sequence(void) {
    int pair[2], enabled = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    assert(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled) == 0);
    int pidfd = (int)syscall(SYS_pidfd_open, getpid(), 0);
    if (pidfd < 0 && (errno == ENOSYS || errno == EPERM)) { close(pair[0]); close(pair[1]); _exit(77); }
    assert(pidfd >= 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 99) == 0);
    pthread_t threads[4]; struct thread_args args[4];
    for (unsigned i = 0; i < 4; ++i) {
        args[i] = (struct thread_args){ .sender = &sender, .duration = (i + 1) * 100, .result = -1 };
        assert(pthread_create(&threads[i], NULL, concurrent_on, &args[i]) == 0);
    }
    for (unsigned i = 0; i < 4; ++i) { assert(pthread_join(threads[i], NULL) == 0); assert(args[i].result == 0); }
    unsigned seen = 0;
    for (unsigned i = 0; i < 4; ++i) {
        ready(pair[1]);
        int result = rme_host_read_vibration(pair[1], pidfd, getpid(), getuid(), 99, i + 1);
        assert(result == 101 || result == 201 || result == 301 || result == 401);
        unsigned bit = 1U << ((result - 101) / 100);
        assert(!(seen & bit)); seen |= bit;
    }
    assert(seen == 15);
    assert(rme_guest_vibration_off(&sender) == 0);
    ready(pair[1]); assert(rme_host_read_vibration(pair[1], pidfd, getpid(), getuid(), 99, 5) == 1);
    assert(rme_guest_vibration_close(&sender) == 0 && rme_guest_vibration_destroy(&sender) == 0);
    close(pidfd); close(pair[1]);
}

static void full_transport_retires_without_retry(void) {
    int pair[2], small = 4096;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    assert(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 101) == 0);
    int result = 0;
    for (unsigned i = 0; i < 10000 && result == 0; ++i) result = rme_guest_vibration_on(&sender, 1);
    assert(result == -EAGAIN || result == -ENOBUFS);
    assert(rme_guest_vibration_off(&sender) == -ESHUTDOWN);
    assert(rme_guest_vibration_close(&sender) == 0 && rme_guest_vibration_destroy(&sender) == 0);
    close(pair[1]);
}

int main(void) {
    alarm(15);
    invalid_setup_retains_fd();
    initialized_descriptor_sets_close_on_exec();
    disconnected_retires_sender();
    sequence_exhaustion_and_idempotent_close();
    concurrent_sends_keep_sequence();
    full_transport_retires_without_retry();
    real_cross_process();
    puts("GUEST_VIBRATION_SENDER_OK");
    return 0;
}
