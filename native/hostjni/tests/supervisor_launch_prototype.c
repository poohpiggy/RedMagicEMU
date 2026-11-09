#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* A fixed-command test of SCM_RIGHTS and process ancestry, not a protocol
 * suitable for real, untrusted or arbitrary Guest commands. */
struct test_request { char cwd[256]; };
struct test_reply { int pid; int status; };

int test_supervisor_one_launch(int control) {
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) return -1;
    struct test_request request = {0};
    struct iovec data = {&request, sizeof request};
    char ancillary[CMSG_SPACE(3 * sizeof(int))] = {0};
    struct msghdr msg = {.msg_iov=&data, .msg_iovlen=1, .msg_control=ancillary, .msg_controllen=sizeof ancillary};
    if (recvmsg(control, &msg, MSG_CMSG_CLOEXEC) != sizeof request ||
        msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC) || !memchr(request.cwd, 0, sizeof request.cwd) || request.cwd[0] != '/') return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    if (!c || c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS || c->cmsg_len != CMSG_LEN(3 * sizeof(int)) || CMSG_NXTHDR(&msg, c)) return -1;
    int fds[3]; memcpy(fds, CMSG_DATA(c), sizeof fds);
    int cwd = open(request.cwd, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (cwd < 0) return -1;
    int exec_status[2]; if (pipe2(exec_status, O_CLOEXEC) != 0) return -1;
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        close(exec_status[0]);
        alarm(8);
        if (fchdir(cwd) != 0 || dup2(fds[0], 0) < 0 || dup2(fds[1], 1) < 0 || dup2(fds[2], 2) < 0) {
            write(exec_status[1], "E", 1); _exit(125);
        }
        close(cwd); for (int i = 0; i < 3; i++) close(fds[i]); close(control);
        char *const argv[] = {"sh", "-c", "IFS= read -r x; printf 'stdin=%s env=%s cwd=%s parent=%s\\n' \"$x\" \"$RME_SESSION\" \"$(pwd -P)\" \"$PPID\"; printf 'stderr=ok\\n' >&2; exit 37", NULL};
        char *const env[] = {"PATH=/system/bin", "RME_SESSION=synthetic", NULL};
        execve("/system/bin/sh", argv, env);
        write(exec_status[1], "E", 1);
        _exit(127);
    }
    close(exec_status[1]);
    close(cwd); for (int i = 0; i < 3; i++) close(fds[i]);
    char failed;
    ssize_t received = read(exec_status[0], &failed, 1);
    close(exec_status[0]);
    if (received != 0) { kill(child, SIGKILL); waitpid(child, NULL, 0); return -1; }
    struct test_reply started = {(int)child, -1};
    if (send(control, &started, sizeof started, MSG_NOSIGNAL) != sizeof started) {
        kill(child, SIGKILL); waitpid(child, NULL, 0); return -1;
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    struct test_reply reply = {(int)child, WEXITSTATUS(status)};
    return send(control, &reply, sizeof reply, MSG_NOSIGNAL) == sizeof reply ? 0 : -1;
}
