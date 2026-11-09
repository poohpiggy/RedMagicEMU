#define _GNU_SOURCE
#include "../radio_host_gate.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void write64(uint8_t *p, uint64_t n) { for (int i=7; i>=0; --i) { p[i]=(uint8_t)n; n>>=8; } }
static void ack(uint8_t *p, uint64_t sequence, uint8_t status) {
    memset(p,0,24); memcpy(p,"RME1",4); write64(p+4,78); write64(p+12,sequence);
    p[20]=3; p[22]=1; p[23]=status;
}
static void lifecycle_ack(uint8_t *p, uint64_t epoch, uint64_t sequence) {
    ack(p,sequence,1); write64(p+4,epoch);
}
static int ready(int fd) {
    struct pollfd p = { .fd=fd, .events=POLLIN };
    return poll(&p,1,5000)>0 && (p.revents&POLLIN);
}
/* A connected peer is a real child process, not the socketpair creator. */
static void lifecycle_round(int restart) {
    char name[80]; snprintf(name,sizeof(name),"rme-native-restart-%ld-%d",(long)getpid(),restart);
    int server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0), one=1; assert(server>=0);
    assert(!setsockopt(server,SOL_SOCKET,SO_PASSCRED,&one,sizeof(one)));
    struct sockaddr_un address={.sun_family=AF_UNIX}; size_t length=strlen(name);
    memcpy(address.sun_path+1,name,length);
    socklen_t address_len=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1+length);
    assert(!bind(server,(void*)&address,address_len) && !listen(server,1));
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        alarm(10); /* bound any orphan if the test parent fails */
        int socket_fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);
        if (socket_fd<0 || connect(socket_fd,(void*)&address,address_len)) _exit(1);
        uint8_t frame[24]; lifecycle_ack(frame,99,1);
        if (send(socket_fd,frame,sizeof(frame),0)!=(ssize_t)sizeof(frame)) _exit(2);
        char go;
        if (restart==2) {
            close(socket_fd);
            for (;;) pause(); /* socket disconnected while the authenticated process lives */
        }
        if (restart==1) {
            if (recv(socket_fd,&go,1,0)!=1) _exit(3);
            lifecycle_ack(frame,100,1);
            if (send(socket_fd,frame,sizeof(frame),0)!=(ssize_t)sizeof(frame)) _exit(4);
        }
        lifecycle_ack(frame,restart==1?100:99,2);
        if (send(socket_fd,frame,sizeof(frame),0)!=(ssize_t)sizeof(frame)) _exit(5);
        if (recv(socket_fd,&go,1,0)!=1) _exit(6);
        _exit(0);
    }
    assert(ready(server)); int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC); assert(fd>=0); close(server);
    int pidfd=(int)syscall(__NR_pidfd_open,child,0); assert(pidfd>=0);
    assert(ready(fd)); int result=rme_host_read_ack(fd,pidfd,child,getuid(),restart==1?100:99,1);
    if (restart==1) {
        assert(result==-1); /* stale ACK from the previous session */
        assert(send(fd,"G",1,0)==1 && ready(fd));
        assert(rme_host_read_ack(fd,pidfd,child,getuid(),100,1)==1);
    } else assert(result==1);
    assert(ready(fd));
    if (restart==2) {
        assert(!kill(child,0)); /* live process, but its socket has disconnected */
        assert(rme_host_read_ack(fd,pidfd,child,getuid(),99,2)==-1);
    } /* Otherwise, valid sequence 2 is queued before the peer dies. */
    assert(!kill(child,SIGKILL)); int status; assert(waitpid(child,&status,0)==child && WIFSIGNALED(status));
    assert(rme_host_read_ack(fd,pidfd,child,getuid(),restart==1?100:99,2)==-1);
    close(pidfd); close(fd);
}
int main(void) {
    alarm(20);
    int fd[2], one=1;
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd));
    assert(!setsockopt(fd[0],SOL_SOCKET,SO_PASSCRED,&one,sizeof(one)));
    int pidfd=(int)syscall(__NR_pidfd_open,getpid(),0);
    if (pidfd < 0) { puts("RADIO_HOST_PIDFD_UNAVAILABLE"); return 77; }
    uint8_t msg[24]; ack(msg,1,1);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,1)==0);
    assert(send(fd[1],msg,24,0)==24);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,1)==1);
    ack(msg,2,2); assert(send(fd[1],msg,24,0)==24);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,2)==2);
    ack(msg,3,1); pid_t child=fork(); assert(child>=0);
    if (!child) _exit(send(fd[1],msg,24,0)==24 ? 0 : 1);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,3)==-1);
    /* The greeting has a reserved sequence, distinct from event ACK sequence 1. */
    ack(msg,INT64_MAX,1);
    assert(send(fd[1],msg,24,0)==24 && send(fd[1],msg,24,0)==24);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,INT64_MAX)==1);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,1)==-1);
    ack(msg,1,1); assert(send(fd[1],msg,24,0)==24);
    assert(rme_host_read_ack(fd[0],pidfd,getpid(),getuid(),78,1)==1);
    close(pidfd); close(fd[0]); close(fd[1]);
    lifecycle_round(0); lifecycle_round(1); lifecycle_round(2);
    puts("RADIO_HOST_GATE_SOCKET_OK");
}
