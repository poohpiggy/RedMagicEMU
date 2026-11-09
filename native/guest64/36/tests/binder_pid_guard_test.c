#define _GNU_SOURCE
#include "../binder_pid_guard.h"
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void peer_exit_retires_guard(void) {
    int listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0), enabled = 1, gate[2];
    assert(listener >= 0 && pipe(gate) == 0);
    assert(setsockopt(listener, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled) == 0);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    snprintf(address.sun_path + 1, sizeof address.sun_path - 1, "rme-pid-test-%ld", (long)getpid());
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(address.sun_path + 1));
    assert(bind(listener, (struct sockaddr *)&address, len) == 0 && listen(listener, 1) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(gate[1]); close(listener);
        int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (client < 0 || connect(client, (struct sockaddr *)&address, len) != 0 || send(client, "x", 1, 0) != 1) _exit(2);
        char signal; if (read(gate[0], &signal, 1) != 1) _exit(3);
        close(gate[0]); close(client); _exit(0);
    }
    close(gate[0]);
    int accepted = accept4(listener, NULL, NULL, SOCK_CLOEXEC); assert(accepted >= 0);
    struct binder_pid_guard guard = {0};
    assert(bpg_init(&guard, accepted) == 0 && guard.pid == child);
    char body;
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(struct ucred))]; } control = {0};
    struct iovec vector = { .iov_base = &body, .iov_len = 1 };
    struct msghdr message = { .msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof control.bytes };
    assert(recvmsg(accepted, &message, 0) == 1 && bpg_packet(&guard, &message));
    assert(write(gate[1], "x", 1) == 1); close(gate[1]);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!bpg_packet(&guard, &message));
    bpg_close(&guard); close(accepted); close(listener);
}

int main(void) {
    int probed = bpg_probe();
    if (probed != 0 && (errno == ENOSYS || errno == EPERM || errno == ENOPROTOOPT)) return 77;
    assert(probed == 0);
    int pair[2], enabled = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    assert(setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled) == 0);
    struct binder_pid_guard guard = {0};
    int result = bpg_init(&guard, pair[0]);
    if (result != 0 && (errno == ENOSYS || errno == EPERM || errno == ENOPROTOOPT)) return 77;
    assert(result == 0);
    assert(guard.pid == getpid() && guard.uid == getuid());
    assert(send(pair[1], "x", 1, 0) == 1);
    char body;
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(struct ucred))]; } control = {0};
    struct iovec vector = { .iov_base = &body, .iov_len = sizeof body };
    struct msghdr message = { .msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof control.bytes };
    assert(recvmsg(pair[0], &message, 0) == 1);
    assert(bpg_packet(&guard, &message) == 1);
    struct msghdr missing = {0};
    assert(bpg_packet(&guard, &missing) == 0);
    message.msg_flags |= MSG_CTRUNC;
    assert(bpg_packet(&guard, &message) == 0);
    message.msg_flags &= ~MSG_CTRUNC;
    bpg_close(&guard);
    assert(bpg_packet(&guard, &message) == 0);
    close(pair[0]); close(pair[1]);
    peer_exit_retires_guard();
    puts("BINDER_PID_GUARD_OK");
}
