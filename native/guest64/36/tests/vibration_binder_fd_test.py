"""Exercise the production Binder receive function with real local FD passing.

This is a Linux harness, not an Android Binder or hardware test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "bclient.c").read_text(encoding="utf-8")
definitions = source[source.index("struct bwr {"):source.index("static char g_bsock")]
receive = source[source.index("static long bread("):source.index("static long bwrite_read(")]

harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
#define NR_recvmsg 1
#define NR_close 2
#define NR_pread64 3
#define NR_exit_group 4
#define NR_shutdown 5
static long host_one(long nr, long arg) {
    if (nr == NR_exit_group) _exit((int)arg);
    return close((int)arg);
}
#define S1(n,a) host_one(n,a)
#define S2(n,a,b) shutdown((int)(a),(int)(b))
static void say(const char *message) { (void)message; }
static long host_recv(int fd, void *message, int flags) {
    long r = recvmsg(fd, message, flags); return r < 0 ? -errno : r;
}
static long host_pread(int fd, void *buffer, size_t n, off_t offset) {
    long r = pread(fd, buffer, n, offset); return r < 0 ? -errno : r;
}
#define S3(n,a,b,c) host_recv((int)(a), (void *)(b), (int)(c))
#define S4(n,a,b,c,d) host_pread((int)(a), (void *)(b), (size_t)(c), (off_t)(d))
#include "bproto.h"
static int g_vibrator_preload;
''' + definitions + r'''
_Static_assert(sizeof(struct msgh) == sizeof(struct msghdr), "msghdr ABI mismatch");
_Static_assert(offsetof(struct msgh, control) == offsetof(struct msghdr, msg_control), "control ABI mismatch");
_Static_assert(offsetof(struct msgh, flags) == offsetof(struct msghdr, msg_flags), "flags ABI mismatch");
#define BR_NOOP 0x0000720cu
#define BR_TRANSACTION 0x80407202u
#define BR_TRANSACTION_SEC_CTX 0x80487202u
#define T_FD 0x66642a85u
#define T_FDA 0x66646185u
#define T_PTR 0x70742a85u
#define SCRATCH 4096
struct bth { int sock; char *scratch; };
static char storage[256];
static void *bscratch(struct bth *t) { return t->scratch; }
static void *balloc(u64 size, u64 id) { (void)id; return size <= sizeof storage ? storage : NULL; }
static int wants_sid(u64 ptr) { (void)ptr; return 0; }
#define bct_log(t, phase) ((void)0)
''' + receive + r'''
static int fd_count(void) {
    DIR *dir = opendir("/proc/self/fd"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    closedir(dir); return count;
}
static void send_big(int sock, int short_header, int truncate, int wake) {
    int fd = memfd_create("aemu-binder-test", MFD_CLOEXEC); assert(fd >= 0);
    assert(write(fd, "12345678", 8) == 8);
    struct d_hdr header = { .flags = F_TXN | F_BIG | (wake ? F_WAKE : 0), .cmd = BR_TRANSACTION, .data_size = 8 };
    struct iovec iov = { &header, short_header ? 1 : sizeof header };
    union { struct cmsghdr alignment; char data[CMSG_SPACE(253 * sizeof(int))]; } control;
    memset(&control, 0, sizeof control);
    int count = truncate ? 253 : 1;
    int handles[253]; for (int i = 0; i < count; ++i) handles[i] = fd;
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
                              .msg_control = control.data, .msg_controllen = CMSG_SPACE((size_t)count * sizeof(int)) };
    struct cmsghdr *c = CMSG_FIRSTHDR(&message);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN((size_t)count * sizeof(int));
    memcpy(CMSG_DATA(c), handles, (size_t)count * sizeof(int));
    assert(sendmsg(sock, &message, 0) == (ssize_t)iov.iov_len);
    close(fd);
}
static void run(int guarded, int short_header, int truncate, int wake) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    if (truncate) { int one = 1; assert(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof one) == 0); }
    if (guarded) {
        pid_t target = fork(); assert(target >= 0);
        if (!target) {
            close(pair[0]);
            char scratch[4096]; u32 output[128] = {0};
            struct bth thread = { .sock = pair[1], .scratch = scratch };
            struct bwr work = { .read_buffer = (u64)output, .read_size = sizeof output };
            g_vibrator_preload = 1;
            (void)bread(&thread, &work);
            _exit(23);
        }
        close(pair[1]); send_big(pair[0], short_header, truncate, wake);
        int status; assert(waitpid(target, &status, 0) == target && WIFEXITED(status) && WEXITSTATUS(status) == 127);
        char marker; assert(recv(pair[0], &marker, 1, MSG_DONTWAIT) == 0);
        close(pair[0]); return;
    }
    char scratch[4096]; u32 output[128] = {0};
    struct bth thread = { .sock = pair[1], .scratch = scratch };
    struct bwr work = { .read_buffer = (u64)output, .read_size = sizeof output };
    send_big(pair[0], short_header, truncate, wake);
    int received_fd = dup(pair[1]); assert(received_fd >= 0); close(received_fd);
    int before = fd_count();
    g_vibrator_preload = guarded;
    long result = bread(&thread, &work);
    assert(!short_header && result == 0 && work.read_consumed > 0 && !memcmp(storage, "12345678", 8));
    errno = 0; assert(fcntl(received_fd, F_GETFD) == -1 && errno == EBADF);
    assert(fd_count() == before);
    close(pair[0]); close(pair[1]);
}
static void missing_big_fd(void) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    pid_t target = fork(); assert(target >= 0);
    if (!target) {
        close(pair[0]);
        char scratch[4096]; u32 output[128] = {0};
        struct bth thread = { .sock = pair[1], .scratch = scratch };
        struct bwr work = { .read_buffer = (u64)output, .read_size = sizeof output };
        g_vibrator_preload = 1;
        (void)bread(&thread, &work);
        _exit(23);
    }
    close(pair[1]);
    struct d_hdr header = { .flags = F_TXN | F_BIG, .cmd = BR_TRANSACTION, .data_size = 8 };
    assert(send(pair[0], &header, sizeof header, MSG_NOSIGNAL) == sizeof header);
    int status; assert(waitpid(target, &status, 0) == target);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 127) {
        fprintf(stderr, "F_BIG without an FD did not retire target\n");
        exit(1);
    }
    char marker; assert(recv(pair[0], &marker, 1, MSG_DONTWAIT) == 0);
    close(pair[0]);
}

static void retirement_with_inherited_alias(void) {
    int pair[2], release[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    assert(pipe(release) == 0);
    pid_t holder = fork(); assert(holder >= 0);
    if (!holder) {
        close(pair[0]); close(release[1]);
        char marker; (void)read(release[0], &marker, 1);
        close(pair[1]); close(release[0]); _exit(0);
    }
    pid_t target = fork(); assert(target >= 0);
    if (!target) {
        close(pair[0]); close(release[0]); close(release[1]);
        char scratch[4096]; u32 output[128] = {0};
        struct bth thread = { .sock = pair[1], .scratch = scratch };
        struct bwr work = { .read_buffer = (u64)output, .read_size = sizeof output };
        g_vibrator_preload = 1;
        (void)bread(&thread, &work);
        _exit(23); /* The target must not continue after consuming rejected IPC. */
    }
    close(pair[1]); close(release[0]);
    send_big(pair[0], 0, 0, 0);
    int status; assert(waitpid(target, &status, 0) == target);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 127) {
        (void)write(release[1], "x", 1); close(release[1]);
        (void)waitpid(holder, &status, 0);
        fprintf(stderr, "rejected Binder transaction did not retire target\n");
        exit(1);
    }
    char ch; assert(recv(pair[0], &ch, 1, MSG_DONTWAIT) == 0); /* peer observes shutdown despite alias */
    assert(write(release[1], "x", 1) == 1); close(release[1]);
    assert(waitpid(holder, &status, 0) == holder && WIFEXITED(status));
    close(pair[0]);
}
int main(void) {
    run(0, 0, 0, 0); /* Existing Binder processes retain F_BIG behavior. */
    run(1, 0, 0, 0); /* Experimental vibrator fails closed on F_BIG. */
    run(1, 1, 0, 0); /* Even a short/failed frame cannot leak its FD. */
    run(1, 0, 0, 1); /* Reject F_WAKE carrying an FD rather than looping. */
    run(1, 0, 1, 0); /* Close all delivered FDs even on control truncation. */
    missing_big_fd(); /* Reject F_BIG even when its required memfd is absent. */
    retirement_with_inherited_alias();
    puts("VIBRATION_BINDER_FD_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-vibration-binder-") as temporary:
    temp = Path(temporary)
    (temp / "binder.c").write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    str(temp / "binder.c"), "-o", str(temp / "binder")], check=True)
    subprocess.run([str(temp / "binder")], check=True)
