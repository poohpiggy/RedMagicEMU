#define _GNU_SOURCE
#include "../vibration_host_gate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

static void write64(uint8_t *p, uint64_t n) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)n; n >>= 8; }
}

static void frame(uint8_t p[28], uint64_t epoch, uint64_t sequence,
                  uint8_t op, uint32_t duration) {
    memset(p, 0, 28); memcpy(p, "RME1", 4);
    write64(p + 4, epoch); write64(p + 12, sequence);
    p[20] = 1; p[22] = 5; p[23] = op;
    p[24] = duration >> 24; p[25] = duration >> 16;
    p[26] = duration >> 8; p[27] = duration;
}

int main(void) {
    int fd[2], one = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fd) == 0);
    assert(setsockopt(fd[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) == 0);
    int pidfd = (int)syscall(__NR_pidfd_open, getpid(), 0);
    if (pidfd < 0) { puts("VIBRATION_HOST_PIDFD_UNAVAILABLE"); return 77; }
    uint8_t msg[28];
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 1) == 0);
    frame(msg, 78, 1, 1, 500);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 1) == 501);
    frame(msg, 78, 2, 2, 0);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 2) == 1);
    frame(msg, 78, 3, 1, 5000);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 3) == 5001);

    frame(msg, 78, 4, 1, 0);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 78, 4, 1, 5001);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 78, 4, 2, 1);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 78, 4, 3, 0);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 79, 4, 1, 500);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 78, 5, 1, 500);
    assert(send(fd[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    frame(msg, 78, 4, 1, 500);
    assert(send(fd[1], msg, sizeof(msg) - 1, 0) == sizeof(msg) - 1);
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 0) == -1);
    assert(rme_host_read_vibration(fd[0], -1, getpid(), getuid(), 78, 4) == -1);
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid() + 1, 78, 4) == -1);
    one = 0;
    assert(setsockopt(fd[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) == 0);
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    one = 1;
    assert(setsockopt(fd[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) == 0);
    close(fd[1]);
    assert(rme_host_read_vibration(fd[0], pidfd, getpid(), getuid(), 78, 4) == -1);
    close(pidfd); close(fd[0]);
    puts("VIBRATION_HOST_GATE_OK");
}
