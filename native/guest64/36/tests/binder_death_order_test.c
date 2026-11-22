#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>

static void change_death(struct thread *thread, u32 command, u32 handle, u64 cookie) {
    u32 request[4] = {command, handle};
    memcpy(request + 2, &cookie, sizeof cookie);
    do_bcs(thread, request, 4);
}

static void acknowledge_death(struct thread *thread, u64 cookie) {
    u32 request[3] = {BC_DEAD_BINDER_DONE};
    memcpy(request + 1, &cookie, sizeof cookie);
    do_bcs(thread, request, 3);
}

static void expect_reply(struct thread *thread, int peer, u32 command, u64 cookie) {
    char reply[sizeof(struct d_hdr) + 12];
    struct d_hdr header;
    u32 actual_command;
    u64 actual_cookie;
    thread->waiting = 1;
    deliver(thread);
    assert(recv(peer, reply, sizeof reply, MSG_DONTWAIT) == sizeof reply);
    memcpy(&header, reply, sizeof header);
    memcpy(&actual_command, reply + sizeof header, sizeof actual_command);
    memcpy(&actual_cookie, reply + sizeof header + sizeof actual_command, sizeof actual_cookie);
    assert(header.br_size == 12);
    assert(actual_command == command);
    assert(actual_cookie == cookie);
}

static void case_death_clear(int clear_before_delivery) {
    int clear_socket[2], notify_socket[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, clear_socket) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, notify_socket) == 0);
    struct proc watcher = { .pid = 101 };
    struct proc *owner = zalloc(sizeof *owner);
    owner->pid = 102;
    struct thread clear = { .fd = clear_socket[0], .proc = &watcher, .read_size = 4096 };
    struct thread notify = { .fd = notify_socket[0], .proc = &watcher, .read_size = 4096 };
    struct node *node = node_get(owner, 1, 2, 1);
    struct ref *ref = ref_for_node(&watcher, node);
    const u64 cookie = UINT64_C(0x1234567);
    change_death(&clear, BC_REQUEST_DEATH_NOTIFICATION, ref->desc, cookie);
    proc_release(owner);
    if (!clear_before_delivery)
        expect_reply(&notify, notify_socket[1], BR_DEAD_BINDER, cookie);

    change_death(&clear, BC_CLEAR_DEATH_NOTIFICATION, ref->desc, cookie);
    assert(!clear.todo.head && "clear completion must wait until death is acknowledged");
    acknowledge_death(&notify, cookie + 1);
    assert(!notify.todo.head && "unrelated acknowledgements cannot clear a notification");
    if (clear_before_delivery) {
        acknowledge_death(&notify, cookie);
        assert(!notify.todo.head && "queued death cannot be acknowledged before delivery");
        expect_reply(&notify, notify_socket[1], BR_DEAD_BINDER, cookie);
    }
    acknowledge_death(&notify, cookie);
    expect_reply(&notify, notify_socket[1], BR_CLEAR_DEATH_NOTIFICATION_DONE, cookie);
    acknowledge_death(&notify, cookie);
    assert(!notify.todo.head && !watcher.todo.head);
    ref_delete(ref);
    free(watcher.refs);
    for (int i = 0; i < 2; ++i) {
        close(clear_socket[i]);
        close(notify_socket[i]);
    }
}

static void case_clear_live_reference(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) == 0);
    struct proc watcher = { .pid = 101 };
    struct proc *owner = zalloc(sizeof *owner);
    struct thread thread = { .fd = sockets[0], .proc = &watcher, .read_size = 4096 };
    struct ref *ref = ref_for_node(&watcher, node_get(owner, 1, 2, 1));
    const u64 cookie = UINT64_C(0xfedcba9);
    change_death(&thread, BC_REQUEST_DEATH_NOTIFICATION, ref->desc, cookie);
    change_death(&thread, BC_CLEAR_DEATH_NOTIFICATION, ref->desc, cookie + 1);
    assert(!thread.todo.head && "wrong cookie must leave the live registration intact");
    change_death(&thread, BC_CLEAR_DEATH_NOTIFICATION, ref->desc, cookie);
    expect_reply(&thread, sockets[1], BR_CLEAR_DEATH_NOTIFICATION_DONE, cookie);
    proc_release(owner);
    assert(!watcher.todo.head && "a cleared registration must not generate a death");
    ref_delete(ref);
    free(watcher.refs);
    close(sockets[0]);
    close(sockets[1]);
}

static void case_release_registration(int after_delivery) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) == 0);
    struct proc watcher = { .pid = 101 };
    struct proc *owner = zalloc(sizeof *owner);
    owner->pid = 102;
    struct thread thread = { .fd = sockets[0], .proc = &watcher, .read_size = 4096 };
    struct ref *ref = ref_for_node(&watcher, node_get(owner, 1, 2, 1));
    ref_inc(ref, 0);
    const u64 cookie = UINT64_C(0x7ffe);
    change_death(&thread, BC_REQUEST_DEATH_NOTIFICATION, ref->desc, cookie);
    proc_release(owner);
    if (after_delivery) expect_reply(&thread, sockets[1], BR_DEAD_BINDER, cookie);
    ref_dec(ref, 0);
    acknowledge_death(&thread, cookie);
    assert(!watcher.todo.head && !thread.todo.head);
    free(watcher.refs);
    close(sockets[0]);
    close(sockets[1]);
}

int main(void) {
    case_death_clear(1);
    case_death_clear(0);
    case_clear_live_reference();
    case_release_registration(0);
    case_release_registration(1);
    puts("Binder death notification ordering passed");
    return 0;
}

