#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../display_trace.h"

static void put32(unsigned char *p, int32_t n) { memcpy(p, &n, 4); }
static void put64(unsigned char *p, uint64_t n) { memcpy(p, &n, 8); }

int main(void) {
    unsigned phases = 0;
    assert(display_trace_once(&phases, 0));
    assert(!display_trace_once(&phases, 0));
    assert(display_trace_once(&phases, 1));
    assert(!display_trace_once(&phases, 32));
    struct display_trace_tracker tracker = {0};
    struct display_trace_event event = { .code = 13, .server = DISPLAY_EVENT_PRESENT, .translated = DISPLAY_TRANSLATED_PRESENT };
    assert(display_trace_record(&tracker, &event));
    for (int i = 0; i < 12; i++) assert(!display_trace_record(&tracker, &event));
    assert(tracker.count[1] == 13);
    event.server = DISPLAY_EVENT_NULL; event.translated = DISPLAY_TRANSLATED_NULL;
    assert(display_trace_record(&tracker, &event));
    assert(!display_trace_record(&tracker, &event));
    for (int i = 0; i < 40; i++) {
        event.server = i % 2 ? DISPLAY_EVENT_PRESENT : DISPLAY_EVENT_NULL;
        event.translated = i % 2 ? DISPLAY_TRANSLATED_PRESENT : DISPLAY_TRANSLATED_NULL;
        (void)display_trace_record(&tracker, &event);
    }
    event.server = DISPLAY_EVENT_UNKNOWN; event.translated = DISPLAY_TRANSLATED_UNKNOWN;
    assert(display_trace_record(&tracker, &event)); /* first abnormal is reserved */
    assert(!display_trace_record(&tracker, &event));
    event.server = DISPLAY_EVENT_TRANSPORT; event.translated = DISPLAY_TRANSLATED_FAILED;
    assert(display_trace_record(&tracker, &event));
    assert(!display_trace_record(&tracker, &event));
    const char source_cmd[] = "/x/libaemu64_36.so\0system_server\0";
    const char target_cmd[] = "/x/libaemu64_36.so\0/system/bin/surfaceflinger\0";
    const char target_wrong[] = "/x/libaemu64_36.so\0/system/bin/surfaceflinger-extra\0";
    assert(display_trace_process_identity("libaemu64_36.so", "/x/libaemu64_36.so", source_cmd, sizeof source_cmd, 0) == DISPLAY_ID_MATCH);
    assert(display_trace_process_identity("system_server", "/x/libaemu64_36.so", NULL, 0, 0) == DISPLAY_ID_MATCH);
    assert(display_trace_process_identity("libaemu64_36.so", "/x/libaemu64_36.so", target_cmd, sizeof target_cmd, 1) == DISPLAY_ID_MATCH);
    assert(display_trace_process_identity("surfaceflinger", "/x/libaemu64_36.so", target_wrong, sizeof target_wrong, 1) == DISPLAY_ID_COMM_DIFFERENT);
    assert(display_trace_process_identity("surfaceflinger", "/x/libaemu64_36.so", target_cmd, sizeof target_cmd - 2, 1) == DISPLAY_ID_COMM_UNAVAILABLE);
    assert(display_trace_process_identity("system_server", "/x/not-runner", source_cmd, sizeof source_cmd, 0) == DISPLAY_ID_EXE_DIFFERENT);
    assert(display_trace_process_identity("system_server", NULL, source_cmd, sizeof source_cmd, 0) == DISPLAY_ID_EXE_UNAVAILABLE);
    unsigned char q[128] = {0};
    const char *name = "android.gui.ISurfaceComposer";
    size_t count = strlen(name);
    put32(q + 8, 0x53595354); put32(q + 12, (int32_t)count);
    for (size_t i = 0; i < count; i++) q[16 + i * 2] = (unsigned char)name[i];
    size_t qlen = 16 + (count + 1) * 2;
    assert(display_trace_request(q, qlen) == 1);
    assert(display_trace_match(1, 12, 0, 1000, 1, 1, q, qlen));
    assert(!display_trace_match(0, 12, 0, 1000, 1, 1, q, qlen));
    assert(!display_trace_match(1, 12, 1, 1000, 1, 1, q, qlen));
    assert(!display_trace_match(1, 13, 0, 0, 1, 1, q, qlen));
    assert(!display_trace_match(1, 12, 0, 1000, 0, 1, q, qlen));
    assert(!display_trace_match(1, 12, 0, 1000, 1, 0, q, qlen));
    assert(!display_trace_match(1, 14, 0, 1000, 1, 1, q, qlen));
    assert(display_trace_request(q, qlen - 1) == 0);
    q[16] = 'X'; assert(display_trace_request(q, qlen) == 0); q[16] = 'a';
    put32(q + 12, INT32_MAX); assert(display_trace_request(q, qlen) == 0);

    unsigned char reply[64] = {0}; struct display_trace_result r;
    put32(reply, 0); put32(reply + 4, 1); put64(reply + 8, 0);
    assert(display_trace_reply(12, reply, 16, NULL, 0, &r) && r.state == DISPLAY_TRACE_OK && r.count == 1 && r.first_id == 0);
    assert(!display_trace_reply(12, reply, 15, NULL, 0, &r));
    put32(reply + 4, 0); assert(display_trace_reply(12, reply, 8, NULL, 0, &r) && r.count == 0);
    put32(reply + 4, INT32_MAX); assert(!display_trace_reply(12, reply, 8, NULL, 0, &r));
    put32(reply, -1); assert(display_trace_reply(12, reply, 4, NULL, 0, &r) && r.state == DISPLAY_TRACE_EXCEPTION && r.exception == -1);
    put32(reply, -127); assert(!display_trace_reply(12, reply, 4, NULL, 0, &r));
    put32(reply, -129); assert(!display_trace_reply(12, reply, 4, NULL, 0, &r));
    memset(reply, 0, sizeof reply); put32(reply, -128); put32(reply + 4, 4); put32(reply + 8, 0);
    assert(display_trace_reply(12, reply, 12, NULL, 0, &r) && r.state == DISPLAY_TRACE_OK && r.count == 0);
    put32(reply + 4, 1000); assert(!display_trace_reply(12, reply, 12, NULL, 0, &r));

    memset(reply, 0, sizeof reply); put32(reply + 4, 0x73622a85); /* null binder */
    assert(display_trace_reply(13, reply, 32, NULL, 0, &r) && r.state == DISPLAY_TRACE_OK && r.binder_present == 0);
    uint64_t offset = 4;
    assert(!display_trace_reply(13, reply, 32, &offset, 8, &r));
    put32(reply + 4, 0x73682a85); put64(reply + 12, 6);
    assert(display_trace_reply(13, reply, 32, &offset, 8, &r) && r.binder_present == 1);
    assert(!display_trace_reply(13, reply, 31, &offset, 8, &r));
    offset = 9; assert(!display_trace_reply(13, reply, 32, &offset, 8, &r));
    return 0;
}
