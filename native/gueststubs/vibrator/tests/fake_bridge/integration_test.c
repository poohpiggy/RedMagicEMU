#define _GNU_SOURCE
#include "fake_guest.h"
#include "../../../../hostjni/vibration_host_gate.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void wait_readable(int fd) {
    struct pollfd item = { .fd = fd, .events = POLLIN };
    assert(poll(&item, 1, 5000) == 1 && (item.revents & POLLIN));
}

static void wait_ack(int fd) {
    char ack;
    wait_readable(fd);
    assert(recv(fd, &ack, 1, 0) == 1 && ack == 'A');
}

static int run_fake_guest(const struct sockaddr_un *address, socklen_t length, int done_fd) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (const struct sockaddr *)address, length) != 0) return 2;
    char ready;
    wait_readable(fd);
    if (recv(fd, &ready, 1, 0) != 1) return 2;
    if (ready == 'S') { close(fd); close(done_fd); return 77; }
    if (ready != 'A') return 2; /* Host enables kernel credentials before accepting messages. */
    struct fake_guest guest = { .fd = fd, .epoch = 786, .sequence = 1 };
    if (fake_guest_on(&guest, 0) == 0 || fake_guest_on(&guest, 5001) == 0 || guest.sequence != 1) return 9;
    if (fake_guest_on(&guest, 500) != 0) return 3;
    wait_ack(fd);
    if (fake_guest_off(&guest) != 0) return 4;
    wait_ack(fd);
    if (fake_guest_on(&guest, 5000) != 0) return 5;
    wait_ack(fd);
    if (fake_guest_off(&guest) != 0) return 6;
    wait_ack(fd);
    fake_guest_stop(&guest);
    if (fake_guest_on(&guest, 100) == 0 || fake_guest_off(&guest) == 0) return 7;
    if (write(done_fd, "D", 1) != 1) return 8;
    wait_ack(fd);
    char eof;
    if (recv(fd, &eof, 1, 0) != 0) return 10;
    struct fake_guest disconnected = { .fd = fd, .epoch = 786, .sequence = 5 };
    if (fake_guest_on(&disconnected, 500) == 0 || disconnected.sequence != 5) return 11;
    close(fd);
    close(done_fd);
    return 0;
}

int main(void) {
    alarm(12);
    int listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0), done[2];
    assert(listener >= 0 && pipe(done) == 0);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    int length = snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1,
                          "rme-vibrator-test-%ld", (long)getpid());
    assert(length > 0 && (size_t)length < sizeof(address.sun_path) - 1);
    socklen_t address_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + length);
    assert(bind(listener, (const struct sockaddr *)&address, address_len) == 0);
    assert(listen(listener, 1) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { close(listener); close(done[0]); _exit(run_fake_guest(&address, address_len, done[1])); }
    close(done[1]);
    wait_readable(listener);
    int connection = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    assert(connection >= 0);
    int one = 1;
    assert(setsockopt(connection, SOL_SOCKET, SO_PASSCRED, &one, sizeof one) == 0);
    int pidfd = (int)syscall(SYS_pidfd_open, child, 0);
    if (pidfd < 0 && (errno == ENOSYS || errno == EPERM)) {
        assert(send(connection, "S", 1, MSG_NOSIGNAL) == 1);
        waitpid(child, NULL, 0);
        close(connection); close(listener); close(done[0]); return 77;
    }
    assert(pidfd >= 0);
    assert(send(connection, "A", 1, MSG_NOSIGNAL) == 1);
    const int expected[] = { 501, 1, 5001, 1 };
    unsigned pulses = 0, cancels = 0;
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        wait_readable(connection);
        int result = rme_host_read_vibration(connection, pidfd, child, getuid(), 786, i + 1);
        assert(result == expected[i]);
        if (result == 1) cancels++; else pulses++; /* Fake sink: never calls hardware. */
        assert(send(connection, "A", 1, MSG_NOSIGNAL) == 1);
    }
    wait_readable(done[0]);
    char done_byte;
    assert(read(done[0], &done_byte, 1) == 1 && done_byte == 'D');
    assert(rme_host_read_vibration(connection, pidfd, child, getuid(), 786, 5) == 0);
    assert(pulses == 2 && cancels == 2);
    assert(send(connection, "A", 1, MSG_NOSIGNAL) == 1);
    assert(shutdown(connection, SHUT_RDWR) == 0);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(pidfd); close(done[0]); close(connection); close(listener);
    puts("VIBRATION_FAKE_GUEST_CROSS_PROCESS_OK");
    return 0;
}
