/* Exercise the daemon's real reply stack and compare forwarded bytes. */
#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>

static void exercise_reply(int code) {
    struct proc client = { .pid = 111 }, server = { .pid = 222 };
    struct thread caller = { .fd = -1, .proc = &client };
    struct thread callee = { .fd = -1, .proc = &server };
    struct txn *request = zalloc(sizeof *request);
    request->memfd = -1; request->from = &caller; request->to_thread = &callee;
    request->sender_pid = client.pid; request->sender_euid = 1000; request->display_trace_code = code;
    caller.stack = request; callee.stack = request;
    struct c_txn m = {0}; m.cmd = BC_REPLY; m.data_size = 16;
    unsigned char data[16] = {0}, before[16];
    int32_t one = 1; memcpy(data + 4, &one, 4); memcpy(before, data, sizeof data);
    do_txn(&callee, &m, (char *)data, sizeof data, NULL, 0);
    assert(!memcmp(data, before, sizeof data));
    assert(caller.stack == NULL && callee.stack == NULL);
    assert(caller.todo.head && caller.todo.head->type == W_TXN);
    struct txn *forwarded = caller.todo.head->t;
    assert(forwarded->data_size == sizeof data && !memcmp(forwarded->data, before, sizeof data));
    txn_free(forwarded); free(wq_pop(&caller.todo));
    assert(callee.todo.head && callee.todo.head->type == W_COMPLETE);
    free(wq_pop(&callee.todo));
}

static void exercise_binder_reply(void) {
    struct proc client = { .pid = 111 }, server = { .pid = 222 };
    struct thread caller = { .fd = -1, .proc = &client };
    struct thread callee = { .fd = -1, .proc = &server };
    struct txn *request = zalloc(sizeof *request);
    request->memfd = -1; request->from = &caller; request->to_thread = &callee;
    request->sender_pid = client.pid; request->sender_euid = 1000; request->display_trace_code = 13;
    caller.stack = request; callee.stack = request;
    struct c_txn m = {0}; m.cmd = BC_REPLY; m.data_size = 32; m.offsets_size = 8;
    unsigned char data[40] = {0};
    struct fbo object = { .type = T_BINDER, .binder = 0x1234, .cookie = 0x5678 };
    uint64_t offset = 4; memcpy(data + 4, &object, sizeof object); memcpy(data + 32, &offset, 8);
    do_txn(&callee, &m, (char *)data, sizeof data, NULL, 0);
    assert(caller.todo.head && caller.todo.head->type == W_TXN);
    struct txn *forwarded = caller.todo.head->t;
    struct display_trace_result result;
    assert(display_trace_reply(13, forwarded->data, 32, forwarded->data + 32, 8, &result));
    assert(result.binder_type == T_HANDLE && result.binder_present);
    assert(display_trace_held_reference(forwarded, &client, &result));
    assert(!display_trace_held_reference(forwarded, &server, &result));
    txn_free(forwarded); free(wq_pop(&caller.todo));
    assert(callee.todo.head && callee.todo.head->type == W_COMPLETE);
    free(wq_pop(&callee.todo));
}

int main(void) {
    display_trace_enabled = 0; display_trace_phase(PHASE_CANDIDATE);
    assert(display_trace_phases == 0);
    display_trace_enabled = 1;
    for (int phase = PHASE_CANDIDATE; phase <= PHASE_MATCH_LIMIT; phase++) {
        display_trace_phase((enum display_phase)phase);
        unsigned seen = display_trace_phases;
        display_trace_phase((enum display_phase)phase);
        assert(display_trace_phases == seen);
    }
    assert(display_trace_phases == (1u << (PHASE_MATCH_LIMIT + 1)) - 1);
    display_trace_enabled = 0;
    struct txn t = {0}; t.data_size = 8; t.memfd = -1;
    assert(display_trace_data_safe(&t, 8, 8));
    assert(!display_trace_data_safe(&t, 8, 7));
    /* AIDL request data has no object offsets, so bclient sends no tail padding. */
    t.data_size = 76; t.offsets_size = 0;
    assert(display_trace_data_safe(&t, 80, 76));
    assert(!display_trace_data_safe(&t, 80, 75));
    t.data_size = 84;
    assert(display_trace_data_safe(&t, 88, 84));
    assert(!display_trace_data_safe(&t, 88, 83));
    /* An actual object offset requires the aligned boundary and its metadata. */
    t.data_size = 76; t.offsets_size = 8;
    assert(display_trace_data_safe(&t, 88, 88));
    assert(!display_trace_data_safe(&t, 88, 84));
    t.memfd = 4; assert(!display_trace_data_safe(&t, 8, 8));
    exercise_reply(0);  /* default off */
    exercise_reply(12); /* matching request, same forwarding */
    exercise_binder_reply();
    return 0;
}
