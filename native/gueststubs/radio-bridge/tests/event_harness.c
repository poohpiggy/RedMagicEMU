#define _GNU_SOURCE
#include "ack.h"
#include "gate.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int server(const char *name, int type) {
    int fd = socket(AF_UNIX, type | SOCK_CLOEXEC, 0); assert(fd >= 0);
    if (type == SOCK_SEQPACKET) { int one = 1; assert(!setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one))); }
    struct sockaddr_un addr = {0}; addr.sun_family = AF_UNIX; size_t n = strlen(name);
    assert(n && n < sizeof(addr.sun_path) - 1); memcpy(addr.sun_path + 1, name, n);
    assert(!bind(fd, (struct sockaddr *)&addr, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n)));
    assert(!listen(fd, 2)); return fd;
}
static void read_exact(int fd, void *buffer, size_t length) {
    size_t offset = 0; while (offset < length) {
        ssize_t n = read(fd, (char *)buffer + offset, length - offset); assert(n > 0); offset += (size_t)n;
    }
}
static uint64_t now_ns(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static void put64(uint8_t *p, uint64_t v) { for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; } }
static size_t frame(uint8_t *p, uint64_t epoch, uint64_t sequence, int kind, const void *payload, size_t length) {
    assert(length && length <= 256); memcpy(p, "RME1", 4); put64(p + 4, epoch); put64(p + 12, sequence);
    p[20] = (uint8_t)kind; p[21] = (uint8_t)(length >> 8); p[22] = (uint8_t)length;
    memcpy(p + 23, payload, length); return 23 + length;
}
static void run(const char *launcher, const char *engine, int iteration, const char *mode) {
    char auth[80], bridge[80], epoch_text[24], host_pid[24];
    snprintf(auth, sizeof(auth), "rme-event-launch-%d-%d", getpid(), iteration);
    snprintf(bridge, sizeof(bridge), "rme-event-bridge-%d-%d", getpid(), iteration);
    uint64_t epoch = 900 + (unsigned)iteration;
    snprintf(epoch_text, sizeof(epoch_text), "%llu", (unsigned long long)epoch);
    snprintf(host_pid, sizeof(host_pid), "%d", getpid());
    int auth_server = server(auth, SOCK_STREAM), bridge_server = server(bridge, SOCK_SEQPACKET);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        alarm(35);
        execl(launcher, launcher, auth, engine, "/system/bin/app_process", "/system/bin/app_process",
              "/system/bin", "app.aemu.stub.Run", "app.aemu.stub.RadioEventSocketTest",
              bridge, epoch_text, host_pid, mode, (char *)0);
        _exit(90);
    }
    int auth_fd = accept4(auth_server, 0, 0, SOCK_CLOEXEC); assert(auth_fd >= 0);
    struct ucred peer = {0}; socklen_t size = sizeof(peer);
    assert(!getsockopt(auth_fd, SOL_SOCKET, SO_PEERCRED, &peer, &size));
    assert(peer.pid == child && peer.uid == getuid());
    int pidfd = (int)syscall(__NR_pidfd_open, child, 0); assert(pidfd >= 0);
    uint8_t challenge[32], response[32]; assert(getrandom(challenge, sizeof(challenge), 0) == sizeof(challenge));
    assert(write(auth_fd, challenge, sizeof(challenge)) == sizeof(challenge));
    read_exact(auth_fd, response, sizeof(response)); assert(!memcmp(challenge, response, sizeof(challenge)));
    assert(write(auth_fd, "G", 1) == 1); close(auth_fd); close(auth_server);
    int connection = accept4(bridge_server, 0, 0, SOCK_CLOEXEC); assert(connection >= 0); close(bridge_server);
    struct rme_gate gate; struct rme_ack_tracker tracker;
    assert(!rme_gate_init(&gate, child, getuid(), epoch) && rme_ack_init(&tracker, epoch));
    const uint8_t pdu[] = {0, 4, 11}, pending[] = {7};
    const char first_number[] = "+10086", second_number[] = "+10087";
    const void *payload[] = {pdu, first_number, second_number, pending};
    const size_t lengths[] = {sizeof(pdu), sizeof(first_number) - 1, sizeof(second_number) - 1, sizeof(pending)};
    enum rme_ack_state statuses[] = {RME_ACK_SUBMITTED, iteration ? RME_ACK_UNCERTAIN : RME_ACK_SUBMITTED,
                                     RME_ACK_REJECTED, RME_ACK_UNCERTAIN};
    uint8_t request[279]; size_t length = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t sequence; assert(rme_ack_begin(&tracker, now_ns(), 10000000000ULL, &sequence));
        length = frame(request, epoch, sequence, i == 0 || i == 3 ? 1 : 2, payload[i], lengths[i]);
        assert(send(connection, request, length, MSG_NOSIGNAL) == (ssize_t)length);
        struct pollfd p = { .fd = connection, .events = POLLIN }; assert(poll(&p, 1, 10000) == 1 && (p.revents & POLLIN));
        assert(rme_ack_receive(&tracker, &gate, connection, pidfd) == 1 && tracker.state == statuses[i]);
    }
    assert(send(connection, request, length, MSG_NOSIGNAL) == (ssize_t)length);
    struct pollfd end = { .fd = connection, .events = POLLIN }; assert(poll(&end, 1, 10000) == 1);
    uint8_t extra[279]; assert(recv(connection, extra, sizeof(extra), MSG_DONTWAIT) == 0);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(connection); close(pidfd);
}
int main(int argc, char **argv) {
    if (argc != 3) return 10;
    alarm(80); run(argv[1], argv[2], 0, "normal"); run(argv[1], argv[2], 1, "partial");
    puts("RADIO_EVENT_CROSS_PROCESS_OK"); return 0;
}
