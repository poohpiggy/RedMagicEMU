#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* Only a local, temporary experiment. The existing Guest starts the Java side. */
static int listener(const char *name, int type) {
    int fd = socket(AF_UNIX, type | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    size_t n = strlen(name);
    if (n == 0 || n >= sizeof(addr.sun_path) - 1) { close(fd); return -1; }
    memcpy(addr.sun_path + 1, name, n);
    if (bind(fd, (struct sockaddr *)&addr, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n)) || listen(fd, 1)) {
        close(fd); return -1;
    }
    return fd;
}
static int ready(int fd) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    return poll(&p, 1, 10000) == 1 && (p.revents & POLLIN);
}
static int read_exact(int fd, void *buffer, size_t len) {
    size_t off = 0;
    while (off < len) {
        if (!ready(fd)) return -1;
        ssize_t n = read(fd, (char *)buffer + off, len - off);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}
static int peer_exists(int fd) {
    struct ucred peer = {0}; socklen_t len = sizeof(peer);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len) == 0 &&
           len == sizeof(peer) && peer.pid > 0;
}
int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) return 2;
    int server = -1, conn = -1, rc = 1, status;
    pid_t attacker = -1;
    uint8_t random_bytes[16], child_bytes[4];
    char socket_name[80];
    if (getrandom(random_bytes, sizeof(random_bytes), 0) != sizeof(random_bytes)) goto done;
    char hex[33];
    for (int i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", random_bytes[i]);
    snprintf(socket_name, sizeof(socket_name), "rme-probe-socket-%s", hex);
    server = listener(socket_name, SOCK_SEQPACKET);
    if (server < 0) goto done;
    printf("PROBE_READY %s %ld %ld\n", socket_name, (long)getpid(), (long)getuid()); fflush(stdout);
    struct pollfd wait = { .fd = server, .events = POLLIN };
    if (poll(&wait, 1, 60000) != 1 || !(wait.revents & POLLIN) ||
        (conn = accept4(server, NULL, NULL, SOCK_CLOEXEC)) < 0 || !peer_exists(conn)) goto done;
    close(server); server = -1;
    char marker;
    if (read_exact(conn, &marker, 1) || marker != 'R' || send(conn, "P", 1, MSG_NOSIGNAL) != 1) goto done;
    attacker = fork();
    if (attacker < 0) goto done;
    if (attacker == 0) _exit(send(conn, "C", 1, MSG_NOSIGNAL) == 1 ? 0 : 1);
    if (read_exact(conn, child_bytes, sizeof(child_bytes))) goto done;
    uint32_t reported = ((uint32_t)child_bytes[0] << 24) | ((uint32_t)child_bytes[1] << 16) |
                        ((uint32_t)child_bytes[2] << 8) | child_bytes[3];
    if (reported != (uint32_t)attacker) goto done;
    pid_t reaped = waitpid(attacker, &status, 0);
    attacker = -1;
    if (reaped < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) goto done;
    puts("RADIO_HOST_CREDENTIALS_PROBE_OK"); rc = 0;
done:
    if (rc) fprintf(stderr, "credential probe failed (errno=%d)\n", errno);
    if (conn >= 0) close(conn);
    if (server >= 0) close(server);
    if (attacker > 0) { kill(attacker, SIGKILL); waitpid(attacker, NULL, 0); }
    return rc;
}
