#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int read_all(int fd, void *buf, size_t size) {
    size_t n = 0;
    while (n < size) {
        ssize_t r = read(fd, (char *)buf + n, size - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        n += (size_t)r;
    }
    return 0;
}

/* A seccomp filter may kill the caller, so probe in this disposable process. */
static int probe_pidfd(void) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) { alarm(3); for (;;) pause(); }
    int fd = (int)syscall(__NR_pidfd_open, child, 0);
    int sent = fd >= 0 && syscall(__NR_pidfd_send_signal, fd, SIGKILL, NULL, 0) == 0;
    if (fd >= 0) close(fd);
    if (!sent) kill(child, SIGKILL); /* still our unreaped child */
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 1;
    return sent && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe-pidfd")) return probe_pidfd();
    if (argc < 3) return 125;
    size_t len = strlen(argv[1]);
    struct sockaddr_un addr = {0};
    if (!len || len >= sizeof(addr.sun_path) - 1) return 125;
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, argv[1], len);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 125;
    struct timeval timeout = {5, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0 ||
        connect(fd, (struct sockaddr *)&addr, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len)) < 0)
        return 125;
    char challenge[32], commit;
    if (read_all(fd, challenge, sizeof challenge) < 0 ||
        send(fd, challenge, sizeof challenge, MSG_NOSIGNAL) != sizeof challenge ||
        read_all(fd, &commit, 1) < 0 || commit != 'G') return 125;
    close(fd);
    execvp(argv[2], argv + 2);
    perror("guest exec");
    return 127;
}
