#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test-only protocol; this is not yet a Guest launcher. */
struct test_request { char cwd[256]; };
struct test_reply { int pid; int status; };
extern int test_supervisor_one_launch(int control);

static int send_request(int socket, const char *cwd, int *fds) {
    struct test_request request = {0};
    if (strlen(cwd) >= sizeof request.cwd) return -1;
    strcpy(request.cwd, cwd);
    struct iovec data = {&request, sizeof request};
    char ancillary[CMSG_SPACE(3 * sizeof(int))] = {0};
    struct msghdr msg = {.msg_iov=&data, .msg_iovlen=1, .msg_control=ancillary, .msg_controllen=sizeof ancillary};
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(3 * sizeof(int));
    memcpy(CMSG_DATA(c), fds, 3 * sizeof(int));
    return sendmsg(socket, &msg, 0) == sizeof request ? 0 : -1;
}
static int read_text(int fd, char *buf, size_t size) {
    ssize_t n = read(fd, buf, size - 1);
    if (n < 0) return -1;
    buf[n] = 0; return 0;
}
int main(int argc, char **argv) {
    if (argc != 2 || prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) return 2;
    signal(SIGPIPE, SIG_IGN);
    int socket[2], in[2], out[2], err[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, socket) || pipe(in) || pipe(out) || pipe(err)) return 2;
    pid_t supervisor = fork();
    if (supervisor < 0) return 2;
    if (!supervisor) {
        close(socket[0]); close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(err[0]); close(err[1]);
        alarm(10); _exit(test_supervisor_one_launch(socket[1]) == 0 ? 0 : 1);
    }
    close(socket[1]);
    int fds[3] = {in[0],out[1],err[1]};
    int ok = send_request(socket[0], argv[1], fds) == 0;
    close(in[0]); close(out[1]); close(err[1]);
    struct test_reply started;
    if (ok && recv(socket[0], &started, sizeof started, 0) != sizeof started) ok = 0;
    if (ok && (started.pid <= 0 || started.status != -1)) ok = 0;
    if (ok && write(in[1], "hello\n", 6) != 6) ok = 0;
    close(in[1]);
    struct test_reply reply;
    if (ok && recv(socket[0], &reply, sizeof reply, 0) != sizeof reply) ok = 0;
    if (ok && (reply.pid != started.pid || reply.status != 37)) ok = 0;
    char output[512] = {0}, errors[128] = {0};
    if (read_text(out[0], output, sizeof output) || read_text(err[0], errors, sizeof errors)) ok = 0;
    close(out[0]); close(err[0]); close(socket[0]);
    char expected[512];
    snprintf(expected, sizeof expected, "stdin=hello env=synthetic cwd=%s parent=%ld\n", argv[1], (long)supervisor);
    if (strcmp(output, expected) || strcmp(errors, "stderr=ok\n")) ok = 0;
    int status;
    if (waitpid(supervisor, &status, 0) != supervisor || !WIFEXITED(status) || WEXITSTATUS(status)) ok = 0;
    while (waitpid(-1, &status, 0) > 0) { }
    printf("SUPERVISED_STDIO_ENV_CWD_EXIT=%d\n", ok);
    return ok ? 0 : 1;
}
