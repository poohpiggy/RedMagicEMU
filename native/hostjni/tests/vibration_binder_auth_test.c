#define _GNU_SOURCE
#include "../vibration_binder_auth.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int s[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, s) == 0);
    int self = (int)syscall(SYS_pidfd_open, getpid(), 0); assert(self >= 0);
    struct rme_binder_auth auth;
    assert(rme_binder_auth_open(&auth, s[0], self, getpid(), getuid()) == 0);
    assert(rme_binder_auth_alive(&auth));
    close(s[0]); close(self);
    assert(rme_binder_auth_alive(&auth));
    rme_binder_auth_close(&auth);
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { for (;;) pause(); }
    int other = (int)syscall(SYS_pidfd_open, child, 0); assert(other >= 0);
    assert(rme_binder_auth_open(&auth, pair[0], other, getpid(), getuid()) != 0);
    assert(rme_binder_auth_open(&auth, pair[0], pair[1], getpid(), getuid()) != 0);
    kill(child, SIGKILL); waitpid(child, 0, 0);
    close(other); close(pair[0]); close(pair[1]); close(s[1]);
    puts("VIBRATION_BINDER_AUTH_OK");
}
