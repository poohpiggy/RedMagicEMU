#define _GNU_SOURCE
#include "gate.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void put64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; }
}
static void frame(uint8_t *p, uint64_t seq) {
    memset(p, 0, 24); memcpy(p, "RME1", 4);
    put64(p + 4, 99); put64(p + 12, seq); p[20] = 1; p[22] = 1; p[23] = 42;
}
static int fd_count(void) {
    DIR *dir = opendir("/proc/self/fd"); assert(dir); int n = 0;
    while (readdir(dir)) ++n;
    closedir(dir); return n;
}
int main(void) {
    alarm(10);
    int sv[2], enabled = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) == 0);
    assert(setsockopt(sv[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    int pidfd = (int)syscall(__NR_pidfd_open, getpid(), 0);
    if (pidfd < 0) { puts("RADIO_BRIDGE_PIDFD_UNAVAILABLE"); return 77; }
    struct rme_gate g; assert(rme_gate_init(&g, getpid(), getuid(), 99) == 0);
    uint8_t msg[512], out[512]; size_t size = 0; frame(msg, 1);
    assert(send(sv[1], msg, 24, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == 1 && size == 24);
    assert(memcmp(out, msg, 24) == 0 && g.last_sequence == 1);
    assert(send(sv[1], msg, 24, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 1);
    frame(msg, 2); memset(msg + 24, 1, sizeof(msg) - 24);
    assert(send(sv[1], msg, sizeof(msg), 0) == sizeof(msg));
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 1);
    int before = fd_count();
    struct iovec iov = { msg, 24 }; char control[CMSG_SPACE(sizeof(int))] = {0};
    struct msghdr header = {0}; header.msg_iov = &iov; header.msg_iovlen = 1;
    header.msg_control = control; header.msg_controllen = sizeof(control);
    struct cmsghdr *c = CMSG_FIRSTHDR(&header); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int)); memcpy(CMSG_DATA(c), &sv[1], sizeof(int));
    assert(sendmsg(sv[1], &header, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 1);
    assert(fd_count() == before);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) _exit(send(sv[1], msg, 24, 0) == 24 ? 0 : 1);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 1);
    assert(send(sv[1], msg, 24, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == 1 && g.last_sequence == 2);
    frame(msg, 3);
    assert(send(sv[1], msg, 24, 0) == 24);
    size = 9;
    assert(rme_gate_receive(&g, sv[0], pidfd, out, 1, &size) == -1 && size == 0 && g.last_sequence == 2);
    assert(send(sv[1], msg, 24, 0) == 24);
    size = 9;
    assert(rme_gate_receive(&g, sv[0], sv[1], out, sizeof(out), &size) == -1 && size == 0 && g.last_sequence == 2);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == 1 && g.last_sequence == 3);
    int disabled = 0;
    assert(setsockopt(sv[0], SOL_SOCKET, SO_PASSCRED, &disabled, sizeof(disabled)) == 0);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 3);
    assert(setsockopt(sv[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    frame(msg, 4); assert(send(sv[1], msg, 24, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == 1 && g.last_sequence == 4);
    int many[32]; for (size_t i = 0; i < 32; ++i) many[i] = sv[1];
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(many))]; } large_control = {0};
    header.msg_control = large_control.bytes; header.msg_controllen = sizeof(large_control.bytes);
    c = CMSG_FIRSTHDR(&header); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(many)); memcpy(CMSG_DATA(c), many, sizeof(many));
    frame(msg, 5); before = fd_count();
    assert(sendmsg(sv[1], &header, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == -1 && g.last_sequence == 4);
    assert(fd_count() == before);
    assert(send(sv[1], msg, 24, 0) == 24);
    assert(rme_gate_receive(&g, sv[0], pidfd, out, sizeof(out), &size) == 1 && g.last_sequence == 5);
    frame(msg, 6); msg[20]=4; msg[22]=16;
    put64(msg+23,99); put64(msg+31,1);
    child=fork(); assert(child>=0);
    if (child==0) _exit(send(sv[1],msg,39,0)==39 ? 0 : 1);
    assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    assert(rme_gate_receive(&g,sv[0],pidfd,out,sizeof(out),&size)==-1 && g.last_sequence==5);
    assert(send(sv[1],msg,39,0)==39);
    assert(rme_gate_receive(&g,sv[0],pidfd,out,sizeof(out),&size)==1 && size==39 &&
           memcmp(out,msg,39)==0 && g.last_sequence==6);
    assert(send(sv[1],msg,39,0)==39);
    assert(rme_gate_receive(&g,sv[0],pidfd,out,sizeof(out),&size)==-1 && g.last_sequence==6);
    close(pidfd); close(sv[0]); close(sv[1]);
    puts("RADIO_BRIDGE_RECV_OK"); return 0;
}
