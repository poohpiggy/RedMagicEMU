#define _GNU_SOURCE
#include "parentage.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t receive_registration(int socket, pid_t expected_sender) {
    pid_t child = -1;
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(struct ucred))]; } ancillary = {0};
    struct iovec vector = { .iov_base = &child, .iov_len = sizeof child };
    struct msghdr message = { .msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = ancillary.bytes, .msg_controllen = sizeof ancillary.bytes };
    assert(recvmsg(socket, &message, 0) == sizeof child);
    assert(!(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)));
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    assert(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_CREDENTIALS);
    assert(header->cmsg_len == CMSG_LEN(sizeof(struct ucred)) && !CMSG_NXTHDR(&message, header));
    struct ucred peer; memcpy(&peer, CMSG_DATA(header), sizeof peer);
    assert(peer.pid == expected_sender && peer.uid == getuid());
    return child;
}

int main(void) {
    alarm(10);
    int socket[2], control[2], one = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, socket) == 0);
    assert(setsockopt(socket[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof one) == 0);
    assert(pipe(control) == 0);
    pid_t parent = fork();
    assert(parent >= 0);
    if (!parent) {
        close(socket[0]); close(control[1]);
        pid_t child = fork();
        if (child < 0) _exit(2);
        if (!child) { char c; close(socket[1]); if (read(control[0], &c, 1) != 1) _exit(3); _exit(0); }
        close(control[0]);
        if (send(socket[1], &child, sizeof child, MSG_NOSIGNAL) != sizeof child) _exit(4);
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) _exit(5);
        if (send(socket[1], "D", 1, MSG_NOSIGNAL) != 1) _exit(6);
        char c; if (recv(socket[1], &c, 1, 0) != 1) _exit(7);
        _exit(0);
    }
    close(socket[1]); close(control[0]);
    int parent_fd = (int)syscall(SYS_pidfd_open, parent, 0);
    if (parent_fd < 0 && (errno == ENOSYS || errno == EPERM)) {
        close(control[1]);
        waitpid(parent, NULL, 0);
        close(socket[0]);
        return 77;
    }
    assert(parent_fd >= 0);
    pid_t child = receive_registration(socket[0], parent);
    int child_fd = (int)syscall(SYS_pidfd_open, child, 0);
    assert(child_fd >= 0);
    assert(rme_observe_parentage(parent_fd, parent, parent, child_fd, child) == 1);
    assert(rme_observe_parentage(-1, parent, parent, child_fd, child) == 0);
    assert(rme_observe_parentage(parent_fd, parent, parent, parent_fd, child) == 0);
    assert(rme_observe_parentage(parent_fd, parent, parent, child_fd, getpid()) == 0);
    assert(write(control[1], "x", 1) == 1); close(control[1]);
    char status; assert(recv(socket[0], &status, 1, 0) == 1 && status == 'D');
    assert(rme_observe_parentage(parent_fd, parent, parent, child_fd, child) == 0);
    assert(send(socket[0], "Q", 1, MSG_NOSIGNAL) == 1);
    int exit_status; assert(waitpid(parent, &exit_status, 0) == parent);
    assert(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0);
    close(parent_fd); close(child_fd); close(socket[0]);
    puts("PARENTAGE_DIAGNOSTIC_OK");
    return 0;
}
