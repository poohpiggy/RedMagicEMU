#define _GNU_SOURCE
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
#include <unistd.h>

static int server(const char *name, int type) {
    int fd = socket(AF_UNIX, type | SOCK_CLOEXEC, 0); assert(fd >= 0);
    if (type == SOCK_SEQPACKET) { int yes = 1; assert(!setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &yes, sizeof(yes))); }
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
static int receive_wait(struct rme_gate *gate, int fd, int pidfd) {
    struct pollfd p = { .fd = fd, .events = POLLIN }; assert(poll(&p, 1, 5000) == 1 && (p.revents & POLLIN));
    uint8_t output[279]; size_t length = 0;
    return rme_gate_receive(gate, fd, pidfd, output, sizeof(output), &length);
}
static int run(const char *launcher, const char *engine, const char *root,
               const char *guest, int iteration, int previous_pidfd) {
    char auth[80], bridge[80], epoch_text[20];
    snprintf(auth, sizeof(auth), "rme-radio-launch-%d-%d", getpid(), iteration);
    snprintf(bridge, sizeof(bridge), "rme-radio-bridge-%d-%d", getpid(), iteration);
    unsigned long long epoch = 99 + (unsigned)iteration;
    snprintf(epoch_text, sizeof(epoch_text), "%llu", epoch);
    int auth_server = server(auth, SOCK_STREAM), bridge_server = server(bridge, SOCK_SEQPACKET);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(!setenv("AEMU64_ROOT", root, 1));
        execl(launcher, launcher, auth, engine, guest, guest, bridge, epoch_text, (char *)0);
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
    struct rme_gate gate; assert(!rme_gate_init(&gate, child, getuid(), epoch));
    uint8_t output[279]; size_t out_length = 0;
    if (previous_pidfd >= 0)
        assert(rme_gate_receive(&gate, connection, previous_pidfd, output, sizeof(output), &out_length) == -1);
    assert(receive_wait(&gate, connection, pidfd) == 1 && gate.last_sequence == 1);
    assert(receive_wait(&gate, connection, pidfd) == -1 && gate.last_sequence == 1);
    assert(receive_wait(&gate, connection, pidfd) == -1 && gate.last_sequence == 1);
    assert(receive_wait(&gate, connection, pidfd) == 1 && gate.last_sequence == 2);
    assert(write(connection, "K", 1) == 1);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    struct pollfd dead = { .fd = pidfd, .events = POLLIN }; assert(poll(&dead, 1, 1000) == 1);
    assert(rme_gate_receive(&gate, connection, pidfd, output, sizeof(output), &out_length) == -1);
    close(connection); return pidfd;
}
int main(int argc, char **argv) {
    if (argc != 5) return 10;
    alarm(45);
    int first = run(argv[1], argv[2], argv[3], argv[4], 0, -1);
    int second = run(argv[1], argv[2], argv[3], argv[4], 1, first);
    close(first); close(second);
    puts("RADIO_BRIDGE_LAUNCHER_GUEST_OK"); return 0;
}
