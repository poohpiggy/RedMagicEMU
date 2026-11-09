#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void put64(uint8_t *p, uint64_t value) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)value; value >>= 8; }
}
static void frame(uint8_t *p, uint64_t epoch, uint64_t seq) {
    memset(p, 0, 24); memcpy(p, "RME1", 4); put64(p + 4, epoch); put64(p + 12, seq);
    p[20] = 1; p[22] = 1; p[23] = 42;
}
int main(int argc, char **argv) {
    if (argc != 3) return 10;
    alarm(15);
    uint64_t epoch = strtoull(argv[2], 0, 10);
    struct sockaddr_un address = {0}; address.sun_family = AF_UNIX;
    size_t length = strlen(argv[1]); if (!length || length >= sizeof(address.sun_path) - 1) return 11;
    memcpy(address.sun_path + 1, argv[1], length);
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address,
            (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + length))) return 12;
    uint8_t data[24]; frame(data, epoch, 1); if (send(fd, data, sizeof(data), 0) != sizeof(data)) return 13;
    frame(data, epoch - 1, 2); if (send(fd, data, sizeof(data), 0) != sizeof(data)) return 14;
    frame(data, epoch, 2);
    pid_t child = fork(); if (child < 0) return 15;
    if (!child) _exit(send(fd, data, sizeof(data), 0) == sizeof(data) ? 0 : 1);
    int status = 0; if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 16;
    if (send(fd, data, sizeof(data), 0) != sizeof(data)) return 17;
    char ack; if (read(fd, &ack, 1) != 1 || ack != 'K') return 18;
    close(fd); return 0;
}
