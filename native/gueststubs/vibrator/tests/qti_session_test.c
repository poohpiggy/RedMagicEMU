#define _GNU_SOURCE
#include "../qti_session.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern int vendor_write(void *, const char *, char *)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
#define W(node, value) vendor_write(NULL, "/sys/class/leds/zte_vibrator/" node, value)
static int listener(char *name) {
    snprintf(name, 80, "rme-qti-session-%ld", (long)getpid());
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0); assert(fd >= 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    size_t n = strlen(name); memcpy(a.sun_path + 1, name, n);
    assert(bind(fd, (struct sockaddr *)&a, offsetof(struct sockaddr_un, sun_path) + 1 + n) == 0);
    assert(listen(fd, 2) == 0); return fd;
}
static void frame(int fd, unsigned op, unsigned duration) {
    unsigned char b[28]; assert(recv(fd, b, sizeof b, 0) == sizeof b);
    assert(!memcmp(b, "RME1", 4) && b[23] == op);
    assert(((unsigned)b[24]<<24 | (unsigned)b[25]<<16 | (unsigned)b[26]<<8 | b[27]) == duration);
}
static void hello(int fd, unsigned long long epoch) {
    unsigned char b[12] = { 'R', 'M', 'E', 'H' };
    for (int i = 11; i >= 4; --i) { b[i] = (unsigned char)epoch; epoch >>= 8; }
    assert(send(fd, b, sizeof b, MSG_NOSIGNAL) == sizeof b);
}
static void hello_with_fd(int fd) {
    unsigned char b[12] = { 'R', 'M', 'E', 'H', 0, 0, 0, 0, 0, 0, 0, 42 };
    int extra = open("/dev/null", O_RDONLY | O_CLOEXEC); assert(extra >= 0);
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = { .iov_base = b, .iov_len = sizeof(b) };
    struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control) };
    struct cmsghdr *h = CMSG_FIRSTHDR(&m); h->cmsg_level = SOL_SOCKET; h->cmsg_type = SCM_RIGHTS; h->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(h), &extra, sizeof(extra));
    assert(sendmsg(fd, &m, MSG_NOSIGNAL) == sizeof(b)); close(extra);
}
static int fd_count(void) {
    int count = 0;
    for (int fd = 0; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}
static void check_status(pid_t p) {
    int status; assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
/* The host may attach after the old two-second Guest deadline. */
static int delayed_handshake(int listener_fd, const char *name, pid_t host, uid_t uid) {
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(listener_fd);
        struct rme_qti_session session = {0};
        if (rme_qti_session_start(&session, name, host, uid, 42) != 0) _exit(1);
        if (rme_qti_session_close(&session) != 0) _exit(2);
        _exit(0);
    }
    int accepted = accept4(listener_fd, NULL, NULL, SOCK_CLOEXEC); assert(accepted >= 0);
    sleep(3);
    int status;
    pid_t early = waitpid(child, &status, WNOHANG);
    if (early != 0) {
        fprintf(stderr, "Guest session exited before delayed host handshake\n");
        close(accepted);
        return 1;
    }
    hello(accepted, 42);
    frame(accepted, 2, 0);
    close(accepted);
    check_status(child);
    return 0;
}
int main(void) {
    assert(rme_qti_session_start(NULL, "test", 1, getuid(), 42) == -EINVAL);
    char name[80]; int l = listener(name); pid_t host = getpid(); uid_t uid = getuid();
    if (delayed_handshake(l, name, host, uid)) return 1;
    pid_t p = fork(); assert(p >= 0);
    if (!p) {
        close(l); struct rme_qti_session s = {0};
        assert(rme_qti_session_start(&s, name, host, uid, 42) == 0);
        assert(rme_qti_session_start(&s, name, host, uid, 42) == -EALREADY);
        assert(W("gain", "0x32") == 0 && W("activate_mode", "5") == 0);
        assert(W("index", "4") == 0 && W("duration_aw", "500\n") == 0);
        assert(W("activate_aw", "1") == 0);
        assert(rme_qti_session_close(&s) == 0);
        assert(W("activate_aw", "1") == -ENODEV);
        _exit(0);
    }
    int c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    hello(c, 42);
    frame(c, 1, 500); frame(c, 2, 0); char b; assert(recv(c, &b, 1, 0) == 0);
    close(c); check_status(p);
    p = fork(); assert(p >= 0);
    if (!p) {
        close(l); struct rme_qti_session s = {0};
        assert(rme_qti_session_start(&s, name, host, uid, 42) == -EACCES);
        assert(W("activate_aw", "1") == -ENODEV); _exit(0);
    }
    c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    hello(c, 43);
    assert(recv(c, &b, 1, 0) == 0); close(c); check_status(p);
    p = fork(); assert(p >= 0);
    if (!p) {
        close(l); struct rme_qti_session s = {0};
        assert(rme_qti_session_start(&s, name, host + 1, uid, 42) == -EACCES);
        assert(W("activate_aw", "1") == -ENODEV); _exit(0);
    }
    c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    hello(c, 42); assert(recv(c, &b, 1, 0) == 0); close(c); check_status(p);
    p = fork(); assert(p >= 0);
    if (!p) {
        close(l); struct rme_qti_session s = {0}; int before = fd_count();
        assert(rme_qti_session_start(&s, name, host, uid, 42) == -EACCES);
        assert(fd_count() == before); _exit(0);
    }
    c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    hello_with_fd(c); assert(recv(c, &b, 1, 0) == 0); close(c); check_status(p);
    p = fork(); assert(p >= 0);
    if (!p) {
        close(l); struct rme_qti_session s = {0};
        assert(rme_qti_session_start(&s, name, host, uid, 42) == -EACCES); _exit(0);
    }
    c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    pid_t imposter = fork(); assert(imposter >= 0);
    if (!imposter) { hello(c, 42); close(c); close(l); _exit(0); }
    check_status(imposter); assert(recv(c, &b, 1, 0) == 0); close(c); check_status(p);
    int syncfd[2]; assert(pipe(syncfd) == 0);
    p = fork(); assert(p >= 0);
    if (!p) {
        close(l); close(syncfd[1]); struct rme_qti_session s = {0};
        assert(rme_qti_session_start(&s, name, host, uid, 42) == 0);
        assert(read(syncfd[0], &b, 1) == 1);
        assert(rme_qti_session_close(&s) < 0);
        assert(rme_qti_session_close(&s) == -ESHUTDOWN);
        assert(W("activate_aw", "1") == -ENODEV);
        close(syncfd[0]); _exit(0);
    }
    close(syncfd[0]); c = accept4(l, NULL, NULL, SOCK_CLOEXEC); assert(c >= 0);
    hello(c, 42);
    close(c); assert(write(syncfd[1], "x", 1) == 1); close(syncfd[1]); check_status(p);
    close(l); puts("QTI_SESSION_OK");
}
