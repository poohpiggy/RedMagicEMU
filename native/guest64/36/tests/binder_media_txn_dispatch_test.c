/* Execute as libaemu64_36.so with two guest argv entries; RME_TEST_MODE selects a case. */
#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

static void check_log(int fd, int matched, int failed, int driver, int multiple) {
    char log[2048];
    close(STDERR_FILENO);
    ssize_t n = read(fd, log, sizeof log - 1);
    assert(n >= 0); log[n] = 0;
    assert(!strstr(log, "NOLEAK"));
    if (!matched) { assert(n == 0); return; }
    assert(strstr(log, "media-binder-txn"));
    assert(strstr(log, driver == 1 ? "driver=main" : "driver=hw"));
    assert(strstr(log, "id=1"));
    if (multiple) assert(strstr(log, "id=2"));
    assert(strstr(log, "phase=request"));
    assert(strstr(log, "phase=request-queued"));
    if (failed) {
        assert(strstr(log, "phase=send-failed"));
        assert(!strstr(log, "phase=request-sent"));
    } else {
        assert(strstr(log, "phase=request-sent"));
        assert(strstr(log, "phase=reply-queued"));
        assert(strstr(log, "phase=reply-sent"));
    }
}

int main(int argc, char **argv) {
    assert(argc == 3);
    const char *s = getenv("RME_TEST_MODE");
    assert(s && *s);
    char *end; long selected = strtol(s, &end, 10);
    assert(!*end && selected >= 0 && selected <= 12);
    int mode = (int)selected;
    assert((strcmp(argv[1], "/system/bin/mediaserver") == 0) == (mode != 3));
    assert((strcmp(argv[2], "/system/bin/mediaserver") == 0) == (mode != 4));
    assert(prctl(PR_SET_NAME, mode == 7 ? "other-thread" : "binder-test", 0, 0, 0) == 0);
    int matched = mode != 0 && mode != 3 && mode != 4 && mode != 5 && mode != 8 && mode != 9 && mode != 10;
    int failed = mode == 2, driver = mode == 6 ? 2 : 1;
    media_txn_trace_enabled = mode != 0;
    media_txn_trace_driver = driver;
    if (mode == 8) media_txn_trace_state.events = 4096;
    if (mode == 12) media_txn_trace_state.events = 4095;
    int logpipe[2]; assert(pipe(logpipe) == 0);
    assert(dup2(logpipe[1], STDERR_FILENO) == STDERR_FILENO); close(logpipe[1]);
    setvbuf(stderr, NULL, _IONBF, 0);
    int cs[2], ss[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, cs) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ss) == 0);
    struct proc client = { .pid = getpid() }, server = { .pid = 222 };
    struct thread caller = { .fd = cs[0], .tid = (int)syscall(SYS_gettid), .proc = &client, .read_size = 4096 };
    struct thread callee = { .fd = ss[0], .tid = 333, .proc = &server, .read_size = 4096 };
    struct node manager = { .owner = &server }; ctx_mgr = &manager;
    if (mode == 9) ctx_mgr = NULL;
    if (mode == 10) manager.owner = NULL;
    struct c_txn m = {0}; m.cmd = BC_TRANSACTION; m.code = 45; m.data_size = 8;
    if (mode == 5) m.tflags = TF_ONE_WAY;
    char payload[8] = "NOLEAK!";
    do_txn(&caller, &m, payload, sizeof payload, NULL, 0);
    if (mode == 9 || mode == 10) {
        assert(!caller.stack && caller.todo.head && caller.todo.head->type == W_ERROR);
        check_log(logpipe[0], 0, 0, driver, 0); return 0;
    }
    if (mode != 5) {
        assert(caller.stack);
        assert((caller.stack->media_txn_trace_id != 0) == matched);
    } else assert(!caller.stack);
    callee.waiting = 1;
    if (failed) callee.fd = -1;
    deliver(&callee);
    if (failed) { check_log(logpipe[0], matched, 1, driver, 0); return 0; }
    char received[512];
    ssize_t n = recv(ss[1], received, sizeof received, 0);
    assert(n >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    struct d_hdr *h = (struct d_hdr *)received;
    assert(h->cmd == BR_TRANSACTION && h->code == 45);
    assert(!memcmp(received + sizeof *h + h->br_size, payload, sizeof payload));
    if (mode == 5) { check_log(logpipe[0], 0, 0, driver, 0); return 0; }
    assert(callee.stack);
    assert((callee.stack->media_txn_trace_id != 0) == matched);
    memset(&m, 0, sizeof m); m.cmd = BC_REPLY; m.data_size = sizeof payload;
    do_txn(&callee, &m, payload, sizeof payload, NULL, 0);
    assert(!caller.stack && !callee.stack);
    assert(caller.todo.tail && caller.todo.tail->t);
    assert((caller.todo.tail->t->media_txn_trace_id != 0) == matched);
    caller.waiting = 1; deliver(&caller);
    n = recv(cs[1], received, sizeof received, 0);
    assert(n >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    h = (struct d_hdr *)received; assert(h->cmd == BR_REPLY);
    assert(!memcmp(received + sizeof *h + h->br_size, payload, sizeof payload));
    if (mode == 11) {
        int cs2[2], ss2[2];
        assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, cs2) == 0);
        assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ss2) == 0);
        struct proc client2 = { .pid = getpid() }, server2 = { .pid = 444 };
        struct thread caller2 = { .fd = cs2[0], .tid = (int)syscall(SYS_gettid), .proc = &client2, .read_size = 4096 };
        struct thread callee2 = { .fd = ss2[0], .tid = 555, .proc = &server2, .read_size = 4096 };
        struct node manager2 = { .owner = &server2 }; ctx_mgr = &manager2;
        memset(&m, 0, sizeof m); m.cmd = BC_TRANSACTION; m.code = 45; m.data_size = sizeof payload;
        do_txn(&caller2, &m, payload, sizeof payload, NULL, 0);
        assert(caller2.stack && caller2.stack->media_txn_trace_id == 2);
        callee2.waiting = 1; deliver(&callee2);
        n = recv(ss2[1], received, sizeof received, 0);
        assert(n >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
        h = (struct d_hdr *)received; assert(h->cmd == BR_TRANSACTION);
        assert(!memcmp(received + sizeof *h + h->br_size, payload, sizeof payload));
        memset(&m, 0, sizeof m); m.cmd = BC_REPLY; m.data_size = sizeof payload;
        do_txn(&callee2, &m, payload, sizeof payload, NULL, 0);
        caller2.waiting = 1; deliver(&caller2);
        n = recv(cs2[1], received, sizeof received, 0);
        assert(n >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
        h = (struct d_hdr *)received; assert(h->cmd == BR_REPLY);
        assert(!memcmp(received + sizeof *h + h->br_size, payload, sizeof payload));
    }
    if (mode == 12) {
        char log[1024]; close(STDERR_FILENO);
        ssize_t count = read(logpipe[0], log, sizeof log - 1);
        assert(count >= 0); log[count] = 0;
        assert(strstr(log, "id=1 phase=request "));
        char *limit = strstr(log, "event limit reached");
        assert(limit && !strstr(limit + 1, "event limit reached"));
        assert(!strstr(log, "phase=request-queued") && !strstr(log, "NOLEAK"));
        return 0;
    }
    check_log(logpipe[0], matched, 0, driver, mode == 11);
    return 0;
}
