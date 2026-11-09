#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* Diagnostic only: run as an N64 Guest process, not a product event source. */
static int readable(int fd, int timeout) {
    struct pollfd p = { .fd = fd, .events = POLLIN | POLLHUP };
    return poll(&p, 1, timeout) == 1 && (p.revents & (POLLIN | POLLHUP));
}
static int byte_in(int fd, char expected) {
    char b; return readable(fd, 10000) && recv(fd, &b, 1, 0) == 1 && b == expected;
}
static int byte_out(int fd, char b) { return send(fd, &b, 1, MSG_NOSIGNAL) == 1; }
static int listener(const char *name) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX }; size_t len = strlen(name);
    if (!len || len >= sizeof(a.sun_path) - 1) { close(fd); return -1; }
    memcpy(a.sun_path + 1, name, len);
    if (bind(fd, (struct sockaddr *)&a, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len)) || listen(fd, 1)) {
        close(fd); return -1;
    }
    return fd;
}
static int send_fd(int socket_fd, int sendfd) {
    char b = 'F'; struct iovec io = { .iov_base = &b, .iov_len = 1 };
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr msg = { .msg_iov = &io, .msg_iovlen = 1,
                          .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes) };
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &sendfd, sizeof(sendfd));
    return sendmsg(socket_fd, &msg, MSG_NOSIGNAL) == 1;
}
int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "parent") && strcmp(argv[1], "child") && strcmp(argv[1], "fd"))) return 2;
    int server = -1, conn = -1, pipefd[2] = {-1,-1}, rc = 1, status;
    pid_t child = -1; uint8_t random[16]; char hex[33], name[80];
    if (getrandom(random, sizeof(random), 0) != sizeof(random)) goto done;
    for (int i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", random[i]);
    snprintf(name, sizeof(name), "rme-transport-%s", hex);
    if ((server = listener(name)) < 0) goto done;
    printf("PROBE_READY %s %ld %ld %s\n", name, (long)getpid(), (long)getuid(), argv[1]); fflush(stdout);
    if (!readable(server, 60000) || (conn = accept4(server, NULL, NULL, SOCK_CLOEXEC)) < 0) goto done;
    close(server); server = -1;
    if (!byte_in(conn, 'R')) goto done;
    if (!strcmp(argv[1], "parent")) {
        if (!byte_out(conn, 'P') || !byte_in(conn, 'A')) goto done;
    } else if (!strcmp(argv[1], "child")) {
        child = fork(); if (child < 0) goto done;
        if (!child) _exit(byte_out(conn, 'C') ? 0 : 1);
        if (!byte_in(conn, 'J')) goto done;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) goto done;
        child = -1;
    } else {
        if (pipe2(pipefd, O_CLOEXEC) || !send_fd(conn, pipefd[1])) goto done;
        close(pipefd[1]); pipefd[1] = -1;
        if (!byte_in(conn, 'J')) goto done;
        /* Receiver still lives. EOF proves it closed its received write descriptor. */
        struct pollfd p = { .fd = pipefd[0], .events = POLLIN | POLLHUP };
        if (poll(&p, 1, 3000) != 1 || !(p.revents & POLLHUP)) goto done;
        puts("RADIO_TRANSPORT_FD_CLOSED_WHILE_RECEIVER_ALIVE");
    }
    if (!byte_out(conn, 'D')) goto done;
    printf("RADIO_TRANSPORT_SENDER_OK mode=%s\n", argv[1]); rc = 0;
done:
    if (rc) fprintf(stderr, "transport probe failed errno=%d\n", errno);
    if (child > 0) { int s; waitpid(child, &s, 0); }
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (pipefd[1] >= 0) close(pipefd[1]);
    if (conn >= 0) close(conn);
    if (server >= 0) close(server);
    return rc;
}
