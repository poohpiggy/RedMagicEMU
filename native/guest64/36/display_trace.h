/* Bounded, read-only decoder for the two SurfaceFlingerAIDL diagnostics. */
#ifndef AEMU_DISPLAY_TRACE_H
#define AEMU_DISPLAY_TRACE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { DISPLAY_TRACE_OK = 1, DISPLAY_TRACE_EXCEPTION = 2 };
struct display_trace_result {
    int state;
    int32_t exception, count;
    uint64_t first_id;
    int binder_present;
    uint32_t binder_type;
    uint64_t binder_value, binder_cookie;
};

enum display_trace_identity_result {
    DISPLAY_ID_MATCH = 1, DISPLAY_ID_COMM_UNAVAILABLE, DISPLAY_ID_COMM_DIFFERENT,
    DISPLAY_ID_EXE_UNAVAILABLE, DISPLAY_ID_EXE_DIFFERENT
};

/* One bit per fixed diagnostic reason, so output is bounded even during a boot loop. */
static int display_trace_once(unsigned *seen, unsigned reason) {
    if (reason >= 32 || (*seen & (1u << reason))) return 0;
    *seen |= 1u << reason; return 1;
}

enum { DISPLAY_EVENT_UNKNOWN, DISPLAY_EVENT_IDS, DISPLAY_EVENT_EMPTY, DISPLAY_EVENT_PRESENT,
       DISPLAY_EVENT_NULL, DISPLAY_EVENT_EXCEPTION, DISPLAY_EVENT_TRANSPORT };
enum { DISPLAY_TRANSLATED_UNKNOWN, DISPLAY_TRANSLATED_OK, DISPLAY_TRANSLATED_PRESENT,
       DISPLAY_TRANSLATED_NULL, DISPLAY_TRANSLATED_FAILED };
struct display_trace_event {
    int code, server, translated, exception, count;
    uint64_t first_id;
};
struct display_trace_tracker {
    uint64_t count[2];
    struct display_trace_event last[2];
    unsigned have[2], abnormal_seen[2], transition_lines;
};

/* Preserve the first occurrence of each abnormal class even after normal-change quota is spent. */
static int display_trace_record(struct display_trace_tracker *tracker, const struct display_trace_event *event) {
    if (event->code != 12 && event->code != 13) return 0;
    int i = event->code - 12;
    if (tracker->count[i] != UINT64_MAX) tracker->count[i]++;
    struct display_trace_event *last = &tracker->last[i];
    int changed = !tracker->have[i] || last->server != event->server || last->translated != event->translated ||
        last->exception != event->exception || last->count != event->count || last->first_id != event->first_id;
    *last = *event; tracker->have[i] = 1;
    unsigned abnormal = 0;
    if (event->server == DISPLAY_EVENT_UNKNOWN) abnormal |= 1u << 0;
    if (event->server == DISPLAY_EVENT_EMPTY) abnormal |= 1u << 1;
    if (event->server == DISPLAY_EVENT_NULL) abnormal |= 1u << 2;
    if (event->server == DISPLAY_EVENT_EXCEPTION) abnormal |= 1u << 3;
    if (event->server == DISPLAY_EVENT_TRANSPORT) abnormal |= 1u << 4;
    if (event->translated == DISPLAY_TRANSLATED_UNKNOWN) abnormal |= 1u << 5;
    if (event->translated == DISPLAY_TRANSLATED_FAILED) abnormal |= 1u << 6;
    unsigned fresh = abnormal & ~tracker->abnormal_seen[i];
    tracker->abnormal_seen[i] |= abnormal;
    if (fresh) return 1;
    if (changed && tracker->transition_lines < 16) { tracker->transition_lines++; return 1; }
    return 0;
}

static enum display_trace_identity_result display_trace_process_identity(const char *comm,
        const char *exe, const void *cmdline, size_t cmdline_size, int target) {
    if (!exe) return DISPLAY_ID_EXE_UNAVAILABLE;
    const char *base = strrchr(exe, '/');
    if (!base || strcmp(base + 1, "libaemu64_36.so")) return DISPLAY_ID_EXE_DIFFERENT;
    if (!target && comm && !strcmp(comm, "system_server")) return DISPLAY_ID_MATCH;
    if (!cmdline || !cmdline_size) return DISPLAY_ID_COMM_UNAVAILABLE;
    const char *args = cmdline;
    const char *end = args + cmdline_size;
    const char *first_end = memchr(args, 0, cmdline_size);
    if (!first_end || first_end + 1 >= end) return DISPLAY_ID_COMM_UNAVAILABLE;
    if (first_end == args) return DISPLAY_ID_COMM_DIFFERENT;
    const char *second = first_end + 1;
    const char *second_end = memchr(second, 0, (size_t)(end - second));
    if (!second_end) return DISPLAY_ID_COMM_UNAVAILABLE;
    const char *expected = target ? "/system/bin/surfaceflinger" : "system_server";
    size_t expected_len = strlen(expected);
    return (size_t)(second_end - second) == expected_len && !memcmp(second, expected, expected_len)
        ? DISPLAY_ID_MATCH : DISPLAY_ID_COMM_DIFFERENT;
}

static int display_trace_i32(const unsigned char *p, size_t n, size_t at, int32_t *v) {
    if (!p || at > n || n - at < 4) return 0;
    memcpy(v, p + at, 4); return 1;
}

static int display_trace_request(const void *data, size_t n) {
    const unsigned char *p = data;
    static const char name[] = "android.gui.ISurfaceComposer";
    int32_t header, count;
    if (!display_trace_i32(p, n, 8, &header) || header != 0x53595354 ||
        !display_trace_i32(p, n, 12, &count) || count != (int32_t)(sizeof name - 1) ||
        n < 16 + sizeof name * 2) return 0;
    for (size_t i = 0; i < sizeof name; i++)
        if (p[16 + i * 2] != (unsigned char)name[i] || p[17 + i * 2] != 0) return 0;
    return 1;
}

static int display_trace_match(int enabled, unsigned code, unsigned flags, unsigned uid,
                               int system_server, int surfaceflinger,
                               const void *data, size_t n) {
    return enabled && (code == 12 || code == 13) && !(flags & 1u) && uid == 1000 &&
           system_server && surfaceflinger && display_trace_request(data, n);
}

static int display_trace_reply(int code, const void *data, size_t n,
                               const void *offsets, size_t offsets_size,
                               struct display_trace_result *r) {
    const unsigned char *p = data;
    size_t at = 4;
    int32_t exception;
    memset(r, 0, sizeof *r);
    if (!display_trace_i32(p, n, 0, &exception)) return 0;
    if (exception == -128) {
        int32_t header_size;
        if (!display_trace_i32(p, n, at, &header_size) || header_size < 4 ||
            (size_t)header_size > n - at) return 0;
        at += (size_t)header_size;
    } else if (exception == -127 || exception == -129) {
        /* Special transport/header values are not ordinary remote exceptions. */
        return 0;
    } else if (exception != 0) {
        r->state = DISPLAY_TRACE_EXCEPTION; r->exception = exception; return 1;
    }
    if (code == 12) {
        int32_t count;
        if (!display_trace_i32(p, n, at, &count) || count < 0) return 0;
        at += 4;
        if ((size_t)count > (n - at) / 8) return 0;
        r->count = count;
        if (count) memcpy(&r->first_id, p + at, 8);
    } else if (code == 13) {
        int32_t type; uint64_t value, cookie;
        /* Flattened binder object plus its 4-byte stability field. */
        if (!display_trace_i32(p, n, at, &type) || at > n || n - at < 28) return 0;
        memcpy(&value, p + at + 8, 8); memcpy(&cookie, p + at + 16, 8);
        r->binder_type = (uint32_t)type; r->binder_value = value; r->binder_cookie = cookie;
        if (offsets_size % 8 || (offsets_size && !offsets)) return 0;
        int found = 0;
        for (size_t i = 0; i < offsets_size; i += 8) {
            uint64_t offset; memcpy(&offset, (const unsigned char *)offsets + i, 8);
            if (offset == at) found = 1;
        }
        if ((uint32_t)type == 0x73622a85u && value == 0 && cookie == 0) {
            if (found) return 0;
            r->binder_present = 0;
        } else {
            if ((uint32_t)type != 0x73622a85u && (uint32_t)type != 0x73682a85u) return 0;
            if (!found) return 0;
            r->binder_present = 1;
        }
    } else return 0;
    r->state = DISPLAY_TRACE_OK; return 1;
}
#endif
