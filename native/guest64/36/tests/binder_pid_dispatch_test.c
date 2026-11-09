#define main binderd_test_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>
#include <dirent.h>

static struct thread *new_thread(int pair[2], int guarded) {
    int passcred = 1;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    if (guarded) assert(setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &passcred, sizeof passcred) == 0);
    struct thread *thread = zalloc(sizeof *thread);
    thread->fd = pair[0];
    if (guarded && bpg_init(&thread->pid_guard, pair[0]) != 0) {
        if (errno == ENOPROTOOPT || errno == EPERM) exit(77);
        assert(!"guard initialization failed");
    }
    thread->pid_guard_enabled = guarded;
    return thread;
}

static void send_hello(struct thread *thread, int peer, int main_thread) {
    struct c_hello hello = { .h = { .op = OP_HELLO }, .pid = (u32)getpid(),
                             .tid = (u32)getpid(), .binderd_test_unused_main = main_thread, .pad = 37 };
    assert(send(peer, &hello, sizeof hello, 0) == sizeof hello);
    on_message(thread);
    assert(procs && procs->pid == getpid() && thread->proc == procs);
}

static void missing_credential_closes_connection(void) {
    int pair[2], passcred = 1;
    struct thread *thread = new_thread(pair, 1);
    send_hello(thread, pair[1], 1);
    /* Losing the required kernel credential must close the connection. */
    passcred = 0;
    assert(setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &passcred, sizeof passcred) == 0);
    struct c_hdr read = { .op = OP_READ };
    assert(send(pair[1], &read, sizeof read, 0) == sizeof read);
    on_message(thread);
    char byte;
    assert(recv(pair[1], &byte, 1, MSG_DONTWAIT) == 0);
    assert(procs == NULL);
    close(pair[1]);
}

static void multiple_connections_and_default_mode(void) {
    int a[2], b[2];
    struct thread *main_thread = new_thread(a, 1), *worker_thread = new_thread(b, 1);
    send_hello(main_thread, a[1], 1);
    send_hello(worker_thread, b[1], 0);
    assert(main_thread->proc == worker_thread->proc);
    struct c_hdr message = { .op = OP_READ };
    assert(send(b[1], &message, sizeof message, 0) == sizeof message);
    on_message(worker_thread);
    assert(procs && worker_thread->proc == procs);
    close(a[1]); on_message(main_thread);
    assert(procs == NULL);
    char byte; assert(recv(b[1], &byte, 1, MSG_DONTWAIT) == 0); close(b[1]);

    int legacy[2];
    struct thread *old = new_thread(legacy, 0);
    send_hello(old, legacy[1], 1);
    assert(send(legacy[1], &message, sizeof message, 0) == sizeof message);
    on_message(old);
    assert(procs && old->proc == procs);
    close(legacy[1]); on_message(old);
    assert(procs == NULL);
}

static void worker_without_main_is_rejected(void) {
    int pair[2];
    struct thread *thread = new_thread(pair, 1);
    struct c_hello hello = { .h = { .op = OP_HELLO }, .pid = (u32)getpid(),
                             .tid = (u32)getpid(), .binderd_test_unused_main = 0, .pad = 37 };
    assert(send(pair[1], &hello, sizeof hello, 0) == sizeof hello);
    on_message(thread);
    char byte; assert(recv(pair[1], &byte, 1, MSG_DONTWAIT) == 0);
    assert(procs == NULL); close(pair[1]);
}

static void failed_watch_closes_owned_descriptors(void) {
    int pair[2];
    struct thread *thread = new_thread(pair, 1);
    int socket_fd = thread->fd, pidfd = thread->pid_guard.pidfd;
    assert(watch_thread(-1, thread) == -1);
    assert(fcntl(socket_fd, F_GETFD) == -1 && errno == EBADF);
    assert(fcntl(pidfd, F_GETFD) == -1 && errno == EBADF);
    char byte; assert(recv(pair[1], &byte, 1, MSG_DONTWAIT) == 0);
    close(pair[1]);
}

static void empty_message_closes_received_fd(void) {
    int pair[2], pipefd[2];
    struct thread *thread = new_thread(pair, 1);
    assert(pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0);
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr message = { .msg_control = control.bytes, .msg_controllen = sizeof control.bytes };
    struct cmsghdr *c = CMSG_FIRSTHDR(&message);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &pipefd[1], sizeof(int));
    assert(sendmsg(pair[1], &message, 0) == 0); close(pipefd[1]);
    on_message(thread);
    char byte; assert(read(pipefd[0], &byte, 1) == 0);
    assert(recv(pair[1], &byte, 1, MSG_DONTWAIT) == 0);
    close(pipefd[0]); close(pair[1]);
}

static void frame_lengths(void) {
    struct { struct c_txn txn; char payload[16]; } frame = {0};
    frame.txn.h.op = OP_TXN; frame.txn.cmd = BC_TRANSACTION; frame.txn.data_size = 3;
    assert(strict_frame_shape((char *)&frame, sizeof frame.txn + 3, NULL, 0));
    assert(!strict_frame_shape((char *)&frame, sizeof frame.txn + 2, NULL, 0));
    /* bclient.bsend inserts five zero bytes between data and offsets. */
    frame.txn.offsets_size = 8;
    assert(strict_frame_shape((char *)&frame, sizeof frame.txn + 16, NULL, 0));
    assert(!strict_frame_shape((char *)&frame, sizeof frame.txn + 11, NULL, 0));
    frame.txn.offsets_size = 1;
    assert(!strict_frame_shape((char *)&frame, sizeof frame, NULL, 0));
    frame.txn.offsets_size = 0; frame.txn.data_size = UINT64_MAX;
    assert(!strict_frame_shape((char *)&frame, sizeof frame, NULL, 0));

    /* Real bclient may omit the aligned tail when no offsets follow. */
    int memory = memfd_create("rme-binder-large-test", MFD_CLOEXEC); assert(memory >= 0);
    frame.txn.h.flags = F_BIG; frame.txn.data_size = INLINE_MAX + 4;
    assert(ftruncate(memory, (off_t)frame.txn.data_size) == 0);
    assert(strict_frame_shape((char *)&frame, sizeof frame.txn, &memory, 1));
    assert(ftruncate(memory, (off_t)((frame.txn.data_size + 7) & ~7ULL)) == 0);
    assert(strict_frame_shape((char *)&frame, sizeof frame.txn, &memory, 1));
    close(memory);
}

static int open_fd_count(void) {
    DIR *directory = opendir("/proc/self/fd"); assert(directory);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') count++;
    closedir(directory); return count;
}

static void snapshot_unaligned_and_short_read(void) {
    const u64 size = INLINE_MAX + 4;
    int source = memfd_create("rme-binder-source", MFD_CLOEXEC); assert(source >= 0);
    assert(ftruncate(source, (off_t)size) == 0);
    assert(pwrite(source, "OK", 2, (off_t)size - 2) == 2);
    int snapshot = strict_snapshot_memfd(source, size, 0);
    assert(snapshot >= 0);
    struct stat st; assert(fstat(snapshot, &st) == 0 && st.st_size == (off_t)((size + 7) & ~7ULL));
    char tail[6]; assert(pread(snapshot, tail, sizeof tail, (off_t)size - 2) == sizeof tail);
    assert(memcmp(tail, "OK\0\0\0\0", sizeof tail) == 0);
    assert(ftruncate(snapshot, 0) == -1);
    close(snapshot);
    assert(ftruncate(source, 2) == 0);
    int count = open_fd_count();
    assert(strict_snapshot_memfd(source, size, 0) == -1);
    assert(open_fd_count() == count);
    close(source);
}

static void snapshot_offsets(void) {
    const u64 size = INLINE_MAX + 4, aligned = (size + 7) & ~7ULL, offsets = 8;
    int source = memfd_create("rme-binder-offset-source", MFD_CLOEXEC); assert(source >= 0);
    assert(ftruncate(source, (off_t)(aligned + offsets)) == 0);
    assert(pwrite(source, "OFFSET!!", 8, (off_t)aligned) == 8);
    int snapshot = strict_snapshot_memfd(source, size, offsets); assert(snapshot >= 0);
    char actual[8]; assert(pread(snapshot, actual, sizeof actual, (off_t)aligned) == sizeof actual);
    assert(memcmp(actual, "OFFSET!!", sizeof actual) == 0);
    close(snapshot); close(source);
}

static void large_transaction_uses_snapshot(void) {
    const u64 size = INLINE_MAX + 4;
    int source = memfd_create("rme-binder-txn-source", MFD_CLOEXEC); assert(source >= 0);
    assert(ftruncate(source, (off_t)size) == 0 && pwrite(source, "OK", 2, (off_t)size - 2) == 2);
    int sender_copy = dup(source); assert(sender_copy >= 0);
    int pipefd[2], pipefd2[2]; assert(pipe(pipefd) == 0 && pipe(pipefd2) == 0);
    int passed[3] = { source, pipefd[1], pipefd2[1] };
    struct proc client = { .pid = getpid() }, server = { .pid = 111 };
    struct thread caller = { .fd = -1, .proc = &client, .pid_guard_enabled = 1 };
    struct node manager = { .owner = &server }; ctx_mgr = &manager;
    struct c_txn message = { .h = { .flags = F_BIG }, .cmd = BC_TRANSACTION, .data_size = size };
    do_txn(&caller, &message, NULL, 0, passed, 3);
    assert(caller.stack && caller.stack->memfd >= 0);
    assert(caller.stack->nfds == 2 && caller.stack->fds[0] == pipefd[1] && caller.stack->fds[1] == pipefd2[1]);
    assert(fcntl(source, F_GETFD) == -1 && errno == EBADF);
    int output = caller.stack->memfd;
    assert(ftruncate(sender_copy, 0) == 0);
    char tail[6]; assert(pread(output, tail, sizeof tail, (off_t)size - 2) == sizeof tail);
    assert(memcmp(tail, "OK\0\0\0\0", sizeof tail) == 0);
    assert(ftruncate(output, 0) == -1);
    close(output); caller.stack->memfd = -1; close(sender_copy);
    close(pipefd[0]); close(pipefd[1]); close(pipefd2[0]); close(pipefd2[1]); caller.stack->nfds = 0; ctx_mgr = NULL;
}

static void large_transaction_short_read_closes_inputs(void) {
    const u64 size = INLINE_MAX + 4;
    int source = memfd_create("rme-binder-short-source", MFD_CLOEXEC); assert(source >= 0);
    assert(ftruncate(source, (off_t)size) == 0);
    struct c_txn message = { .h = { .op = OP_TXN, .flags = F_BIG },
                             .cmd = BC_TRANSACTION, .data_size = size };
    assert(strict_frame_shape((char *)&message, sizeof message, &source, 1));
    assert(ftruncate(source, 2) == 0);
    int pipefd[2]; assert(pipe(pipefd) == 0);
    int passed[2] = { source, pipefd[1] };
    struct proc client = { .pid = getpid() };
    struct thread caller = { .fd = -1, .proc = &client, .pid_guard_enabled = 1 };
    int before = open_fd_count();
    do_txn(&caller, &message, NULL, 0, passed, 2);
    assert(!caller.stack && open_fd_count() == before - 2);
    assert(fcntl(source, F_GETFD) == -1 && errno == EBADF);
    assert(fcntl(pipefd[1], F_GETFD) == -1 && errno == EBADF);
    assert(caller.todo.head && caller.todo.head->type == W_ERROR && caller.todo.head->err == BR_FAILED_REPLY);
    free(wq_pop(&caller.todo)); close(pipefd[0]);
}

int main(void) {
    ep = epoll_create1(EPOLL_CLOEXEC); assert(ep >= 0);
    missing_credential_closes_connection();
    worker_without_main_is_rejected();
    failed_watch_closes_owned_descriptors();
    multiple_connections_and_default_mode();
    empty_message_closes_received_fd();
    frame_lengths();
    snapshot_unaligned_and_short_read();
    snapshot_offsets();
    large_transaction_short_read_closes_inputs();
    large_transaction_uses_snapshot();
    close(ep);
    puts("BINDER_PID_DISPATCH_OK");
}
