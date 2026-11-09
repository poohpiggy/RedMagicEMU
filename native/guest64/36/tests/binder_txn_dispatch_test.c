/* Run with argv[1] equal to the target process name and argv[2] set to a mode 0..9. */
#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

static void check_log(int fd, int enabled, int failed, int token) {
    char log[2048];
    close(STDERR_FILENO);
    ssize_t n = read(fd, log, sizeof log - 1);
    assert(n >= 0); log[n] = 0;
    assert(!strstr(log, "NOLEAK") && !strstr(log, "android.media"));
    if (token) {
        char expect[128];
        snprintf(expect, sizeof expect, "id=1 phase=request-token pid=0 tid=0 handle=0 code=7 detail=%d", token);
        assert(strstr(log, expect));
    } else assert(!strstr(log, "phase=request-token"));
    if (!enabled) assert(n == 0);
    else if (failed) {
        assert(strstr(log, "phase=send-failed"));
        assert(!strstr(log, "phase=request-sent"));
    } else {
        assert(strstr(log, "id=1 phase=request-queued"));
        assert(strstr(log, "id=1 phase=request-sent"));
        assert(strstr(log, "id=1 phase=reply-queued"));
        assert(strstr(log, "id=1 phase=reply-sent"));
    }
}

int main(int argc, char **argv) {
    assert(argc == 3);
    int mode = argv[2][0] - '0';
    assert(mode >= 0 && mode <= 9 && argv[2][1] == 0);
    int matched = mode != 0 && mode != 3 && mode != 5 && mode != 6;
    assert(prctl(PR_SET_NAME, mode == 3 || mode == 6 ? "not-target" : "fin-1-thread-4", 0, 0, 0) == 0);
    binder_txn_trace_enabled = mode != 0 && mode != 5;
    int failed = mode == 2;
    int token = mode >= 4;
    int token_result = !matched || !token ? 0 : mode == 7 ? 2 : (mode == 8 || mode == 9) ? 1 : 3;
    /* Invalid/shared payload cannot be read by the diagnostic. */
    struct txn unsafe = { .memfd = 3, .data_size = 128 };
    assert(!display_trace_data_safe(&unsafe, 128, 128));
    assert(btt_token(1, 7, display_trace_data_safe(&unsafe, 128, 128), (void *)1, 128) == BTT_TOKEN_UNAVAILABLE);
    unsafe.memfd = -1; unsafe.data_size = UINT64_MAX;
    assert(!display_trace_data_safe(&unsafe, 0, 0));
    unsafe.data_size = 128;
    assert(!display_trace_data_safe(&unsafe, 128, 127));
    int logpipe[2]; assert(pipe(logpipe) == 0);
    assert(dup2(logpipe[1], STDERR_FILENO) == STDERR_FILENO); close(logpipe[1]);
    setvbuf(stderr, NULL, _IONBF, 0);
    int cs[2], ss[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, cs) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ss) == 0);
    struct proc client = { .pid = getpid() }, server = { .pid = 222 };
    struct thread caller = { .fd = cs[0], .tid = (int)syscall(SYS_gettid), .proc = &client, .read_size = 4096 };
    struct thread callee = { .fd = ss[0], .tid = 333, .proc = &server, .read_size = 4096 };
    struct node manager = { .owner = &server };
    ctx_mgr = &manager;
    struct c_txn m = {0};
    m.cmd = BC_TRANSACTION; m.code = token ? 7 : 45; m.data_size = 8;
    char payload[96] = "NOLEAK!";
    if (token) {
        static const char name[] = "android.media.IMediaPlayerService";
        uint32_t header = 0x53595354, count = sizeof name - 1;
        memset(payload, 0, sizeof payload); memcpy(payload + 8, &header, 4); memcpy(payload + 12, &count, 4);
        for (size_t i = 0; i < sizeof name; i++) payload[16 + i * 2] = name[i];
        m.data_size = 16 + sizeof name * 2;
        if (mode == 7) payload[16] = 'X';
        if (mode == 8) m.data_size = 16;
    }
    size_t data_size = m.data_size;
    if (mode == 9) {
        int memfd = (int)syscall(SYS_memfd_create, "binder-test", 0);
        assert(memfd >= 0 && write(memfd, payload, sizeof payload) == sizeof payload);
        m.h.flags = F_BIG;
        do_txn(&caller, &m, NULL, 0, &memfd, 1);
    } else do_txn(&caller, &m, payload, sizeof payload, NULL, 0);
    assert(caller.stack);
    unsigned id = caller.stack->binder_txn_trace_id;
    assert((id != 0) == matched);
    assert(binder_txn_trace_state.events == (matched ? (token ? 3u : 2u) : 0u));
    callee.waiting = 1;
    if (failed) callee.fd = -1;
    deliver(&callee);
    if (failed) { check_log(logpipe[0], 1, 1, token_result); return 0; }
    assert(callee.stack && callee.stack->binder_txn_trace_id == id);
    char received[512];
    ssize_t n;
    if (mode == 9) {
        char control[CMSG_SPACE(sizeof(int))];
        struct iovec iov = { received, sizeof received };
        struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof control };
        n = recvmsg(ss[1], &msg, 0);
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        assert(c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len == CMSG_LEN(sizeof(int)));
        int fd; memcpy(&fd, CMSG_DATA(c), sizeof fd);
        char big[96]; assert(pread(fd, big, data_size, 0) == (ssize_t)data_size); close(fd);
        assert(!memcmp(big, payload, data_size));
        assert(n >= (ssize_t)sizeof(struct d_hdr));
    } else {
        n = recv(ss[1], received, sizeof received, 0);
        assert(n >= (ssize_t)(sizeof(struct d_hdr) + data_size));
    }
    struct d_hdr *h = (struct d_hdr *)received;
    assert(h->cmd == BR_TRANSACTION && h->code == (token ? 7u : 45u));
    if (mode != 9) assert(!memcmp(received + sizeof *h + h->br_size, payload, data_size));
    else assert(h->flags & F_BIG);
    assert(binder_txn_trace_state.events == (matched ? (token ? 4u : 3u) : 0u));
    memset(&m, 0, sizeof m); m.cmd = BC_REPLY; m.data_size = data_size;
    do_txn(&callee, &m, payload, sizeof payload, NULL, 0);
    assert(!caller.stack && !callee.stack);
    assert(caller.todo.tail && caller.todo.tail->t->binder_txn_trace_id == id);
    caller.waiting = 1;
    deliver(&caller);
    n = recv(cs[1], received, sizeof received, 0);
    assert(n >= (ssize_t)(sizeof(struct d_hdr) + data_size));
    h = (struct d_hdr *)received;
    assert(h->cmd == BR_REPLY);
    assert(!memcmp(received + sizeof *h + h->br_size, payload, data_size));
    assert(binder_txn_trace_state.events == (matched ? (token ? 6u : 5u) : 0u));
    check_log(logpipe[0], matched, 0, token_result);
    return 0;
}
