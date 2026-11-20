"""Verify the production Binder client releases only borrowed native-handle FDs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "bclient.c").read_text(encoding="utf-8")
definitions = source[source.index("#define BR_NOOP"):source.index("static char g_bsock")]
buffer = source[source.index("// buffers of delivered transactions"):source.index("static int is_binder_fd(")]
receiver = source[source.index("static long bread("):source.index("static long bwrite_read(")]

harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
#define NR_sched_yield SYS_sched_yield
#define NR_mmap SYS_mmap
#define NR_munmap SYS_munmap
#define NR_close SYS_close
#define NR_recvmsg SYS_recvmsg
#define NR_pread64 SYS_pread64
#define NR_shutdown SYS_shutdown
#define NR_exit_group SYS_exit_group
static long raw(long result) { return result < 0 ? -errno : result; }
#define S0(n) raw(syscall(n))
#define S1(n,a) raw(syscall(n,a))
#define S2(n,a,b) raw(syscall(n,a,b))
#define S3(n,a,b,c) raw(syscall(n,a,b,c))
#define S4(n,a,b,c,d) raw(syscall(n,a,b,c,d))
#define S6(n,a,b,c,d,e,f) raw(syscall(n,a,b,c,d,e,f))
#include "bproto.h"
''' + definitions + buffer + r'''
#define BR_TRANSACTION 0x80407202u
#define BR_TRANSACTION_SEC_CTX 0x80487202u
#define SCRATCH (INLINE_MAX + 8192)
struct bth { int sock; char *scratch; };
static int g_vibrator_preload;
static void say(const char *message) { fputs(message, stderr); }
static int wants_sid(u64 ptr) { (void)ptr; return 0; }
static char *bscratch(struct bth *thread) { return thread->scratch; }
#define bct_log(thread, phase) ((void)0)
''' + receiver + r'''
struct payload {
    struct bbo parent;
    struct bfda array;
    u32 plain[6];
    u64 offsets[3];
    int fds[2];
};

static void send_transaction(int sock, int big, int memfd, u64 id) {
    struct payload data = {
        .parent = {.type = T_PTR, .length = sizeof data.fds},
        .array = {.type = T_FDA, .num_fds = 2},
        .plain = {T_FD},
        .offsets = {0, offsetof(struct payload, array), offsetof(struct payload, plain)},
    };
    struct d_hdr header = {.flags = F_TXN, .cmd = BR_TRANSACTION, .id = id, .nfds = 3,
        .data_size = offsetof(struct payload, offsets), .offsets_size = sizeof data.offsets,
        .extra_size = sizeof data.fds};
    int serialized = -1;
    struct iovec iov[2] = {{&header, sizeof header}, {&data, sizeof data}};
    int handles[4] = {memfd, memfd, memfd, memfd};
    if (big) {
        serialized = memfd_create("parcel", MFD_CLOEXEC);
        assert(serialized >= 0 && write(serialized, &data, sizeof data) == sizeof data);
        handles[0] = serialized;
        header.flags |= F_BIG;
    }
    size_t count = big ? 4 : 3;
    union { struct cmsghdr align; char space[CMSG_SPACE(sizeof handles)]; } control = {0};
    struct msghdr msg = {.msg_iov = iov, .msg_iovlen = big ? 1 : 2,
                         .msg_control = control.space, .msg_controllen = CMSG_SPACE(count * sizeof(int))};
    struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
    hdr->cmsg_level = SOL_SOCKET;
    hdr->cmsg_type = SCM_RIGHTS;
    hdr->cmsg_len = CMSG_LEN(count * sizeof(int));
    memcpy(CMSG_DATA(hdr), handles, count * sizeof(int));
    assert(sendmsg(sock, &msg, 0) == (ssize_t)(sizeof header + (big ? 0 : sizeof data)));
    if (serialized >= 0) assert(close(serialized) == 0);
}

static void exercise(int big, int use_pool) {
    char arena[4096] __attribute__((aligned(16))) = {0};
    if (use_pool) {
        g_bmap = (u64)arena;
        g_bmap_size = sizeof arena;
        ((struct bhdr *)arena)->size = sizeof arena - sizeof(struct bhdr);
    }
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    int owner = memfd_create("shared-image", MFD_CLOEXEC);
    assert(owner >= 0 && write(owner, "test", 4) == 4);
    static char scratch[SCRATCH];
    struct bth thread = {.sock = pair[1], .scratch = scratch};
    for (u64 id = 100; id < 105; id++) {
        send_transaction(pair[0], big, owner, id);
        u64 out[128] = {0};
        struct bwr work = {.read_buffer = (u64)out, .read_size = sizeof out};
        assert(bread(&thread, &work) == 0 && work.read_consumed > 0);
        struct btd txn;
        memcpy(&txn, out + 1, sizeof txn);
        struct payload *data = (void *)txn.buffer;
        int first = data->fds[0], second = data->fds[1], plain = (int)data->plain[2];
        assert(first != second && first != plain && second != plain);
        assert(fcntl(first, F_GETFD) >= 0 && fcntl(second, F_GETFD) >= 0 && fcntl(plain, F_GETFD) >= 0);
        int retained = dup(first);
        assert(retained >= 0);
        data->fds[0] = plain;
        data->fds[1] = -1;
        assert(bfree(txn.buffer) == id);
        if (fcntl(first, F_GETFD) != -1 || errno != EBADF || fcntl(second, F_GETFD) != -1 || errno != EBADF) {
            fprintf(stderr, "Binder buffer free leaked borrowed FD array (big=%d, pool=%d)\n", big, use_pool);
            _exit(1);
        }
        char check[4];
        assert(pread(retained, check, sizeof check, 0) == sizeof check && !memcmp(check, "test", 4));
        assert(fcntl(plain, F_GETFD) >= 0);
        assert(close(plain) == 0 && close(retained) == 0);
    }
    close(owner);
    close(pair[0]);
    close(pair[1]);
    g_bmap = g_bmap_size = 0;
}

int main(void) {
    exercise(0, 1);
    exercise(0, 0);
    exercise(1, 1);
    exercise(1, 0);
    puts("BINDER_BUFFER_FD_LIFETIME_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-binder-buffer-fd-") as temp:
    directory = Path(temp)
    src = directory / "test.c"
    exe = directory / "test"
    src.write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
