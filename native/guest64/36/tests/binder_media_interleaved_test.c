/* Execute as libaemu64_36.so with two /system/bin/mediaserver argv fields. */
#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
    assert(argc == 3 && !strcmp(argv[1], "/system/bin/mediaserver") &&
           !strcmp(argv[2], "/system/bin/mediaserver"));
    int logs[2], c1[2], c2[2], s1[2], s2[2];
    assert(pipe(logs) == 0 && dup2(logs[1], STDERR_FILENO) == STDERR_FILENO);
    close(logs[1]); setvbuf(stderr, NULL, _IONBF, 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, c1) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, c2) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, s1) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, s2) == 0);
    struct proc src = { .pid = getpid() }, dst1 = { .pid = 201 }, dst2 = { .pid = 202 };
    struct thread caller1 = { .fd = c1[0], .tid = (int)syscall(SYS_gettid), .proc = &src, .read_size = 4096 };
    struct thread caller2 = { .fd = c2[0], .tid = (int)syscall(SYS_gettid), .proc = &src, .read_size = 4096 };
    struct thread server1 = { .fd = s1[0], .tid = 301, .proc = &dst1, .read_size = 4096 };
    struct thread server2 = { .fd = s2[0], .tid = 302, .proc = &dst2, .read_size = 4096 };
    struct node n1 = { .owner = &dst1 }, n2 = { .owner = &dst2 };
    struct c_txn request = { .cmd = BC_TRANSACTION, .code = 45, .data_size = 8 };
    char payload[8] = "NOLEAK!", recvbuf[512];
    media_txn_trace_enabled = 1; media_txn_trace_driver = 1;
    ctx_mgr = &n1; do_txn(&caller1, &request, payload, sizeof payload, NULL, 0);
    ctx_mgr = &n2; do_txn(&caller2, &request, payload, sizeof payload, NULL, 0);
    assert(caller1.stack && caller1.stack->media_txn_trace_id == 1);
    assert(caller2.stack && caller2.stack->media_txn_trace_id == 2);
    server1.waiting = server2.waiting = 1;
    deliver(&server1); deliver(&server2);
    assert(recv(s1[1], recvbuf, sizeof recvbuf, 0) >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    assert(recv(s2[1], recvbuf, sizeof recvbuf, 0) >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    assert(server1.stack && server1.stack->media_txn_trace_id == 1);
    assert(server2.stack && server2.stack->media_txn_trace_id == 2);
    struct c_txn reply = { .cmd = BC_REPLY, .data_size = sizeof payload };
    do_txn(&server2, &reply, payload, sizeof payload, NULL, 0);
    assert(caller2.todo.tail->t->media_txn_trace_id == 2 && caller1.stack);
    caller2.waiting = 1; deliver(&caller2);
    assert(recv(c2[1], recvbuf, sizeof recvbuf, 0) >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    struct d_hdr *h = (struct d_hdr *)recvbuf;
    assert(h->cmd == BR_REPLY && !memcmp(recvbuf + sizeof *h + h->br_size, payload, sizeof payload));
    do_txn(&server1, &reply, payload, sizeof payload, NULL, 0);
    assert(caller1.todo.tail->t->media_txn_trace_id == 1);
    caller1.waiting = 1; deliver(&caller1);
    assert(recv(c1[1], recvbuf, sizeof recvbuf, 0) >= (ssize_t)(sizeof(struct d_hdr) + sizeof payload));
    h = (struct d_hdr *)recvbuf;
    assert(h->cmd == BR_REPLY && !memcmp(recvbuf + sizeof *h + h->br_size, payload, sizeof payload));
    close(STDERR_FILENO);
    char log[4096]; ssize_t n = read(logs[0], log, sizeof log - 1);
    assert(n >= 0); log[n] = 0;
    assert(!strstr(log, "NOLEAK"));
    char *r2 = strstr(log, "id=2 phase=reply-sent");
    char *r1 = strstr(log, "id=1 phase=reply-sent");
    assert(r1 && r2 && r2 < r1);
    return 0;
}
