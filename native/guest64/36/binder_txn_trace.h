#ifndef AEMU_BINDER_TXN_TRACE_H
#define AEMU_BINDER_TXN_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "binder_client_trace.h"
#pragma GCC diagnostic pop

/* Fixed comparison only: no token contents or arbitrary Parcel fields are logged. */
#define BTT_TOKEN_DIAG_DEFINED 1
enum { BTT_TOKEN_NONE, BTT_TOKEN_UNAVAILABLE, BTT_TOKEN_OTHER, BTT_TOKEN_MATCH };
static inline int btt_token(int enabled, unsigned code, int safe, const void *data, size_t n) {
    static const char name[] = "android.media.IMediaPlayerService";
    const unsigned char *p = data;
    uint32_t header, count;
    if (!enabled || code != 7) return BTT_TOKEN_NONE;
    if (!safe || !p || n < 16) return BTT_TOKEN_UNAVAILABLE;
    memcpy(&header, p + 8, 4); memcpy(&count, p + 12, 4);
    if (header != 0x53595354 || (size_t)count >= (n - 16) / 2) return BTT_TOKEN_UNAVAILABLE;
    size_t terminator = 16 + (size_t)count * 2;
    if (p[terminator] || p[terminator + 1]) return BTT_TOKEN_UNAVAILABLE;
    if (count != sizeof name - 1) return BTT_TOKEN_OTHER;
    for (size_t i = 0; i < sizeof name - 1; i++)
        if (p[16 + i * 2] != (unsigned char)name[i] || p[17 + i * 2] != 0) return BTT_TOKEN_OTHER;
    return BTT_TOKEN_MATCH;
}

/* binderd is single-threaded; the limit is per daemon lifetime. */
struct btt_state { unsigned next_id, events; };
#define BMT_DEFINED 1
#define BMT_LIMIT 4096u
/* Complete argv fields only; the caller separately verifies the engine executable. */
static int bmt_process_identity(const char *buf, unsigned long n, const char *engine, const char *guest) {
    unsigned long i = 0;
    if (!buf || !engine || !*engine || !guest || !*guest) return 0;
    for (unsigned long j = 0; engine[j]; j++, i++)
        if (i >= n || buf[i] != engine[j]) return 0;
    if (i >= n || buf[i++] != 0) return 0;
    for (int arg = 0; arg < 2; arg++) {
        unsigned long j = 0;
        for (; guest[j]; j++, i++)
            if (i >= n || buf[i] != guest[j]) return 0;
        if (i >= n || buf[i++] != 0) return 0;
    }
    return 1;
}
static int bmt_identity(const char *buf, unsigned long n, const char *engine) {
    return bmt_process_identity(buf, n, engine, "/system/bin/mediaserver");
}
#define BMT_HWM_DEFINED 1
static int bmt_hwmanager_identity(const char *buf, unsigned long n, const char *engine) {
    return bmt_process_identity(buf, n, engine, "/system/bin/hwservicemanager");
}
/* The mediaserver diagnostic has its own bounded counter and id space. */
static unsigned bmt_start(struct btt_state *s, int enabled, int identity, unsigned flags) {
    if (!enabled || !identity || (flags & 1u) || s->events >= BMT_LIMIT || s->next_id == ~0u) return 0;
    return ++s->next_id;
}
static int bmt_event(struct btt_state *s, unsigned id) {
    if (!id || s->events >= BMT_LIMIT) return 0;
    s->events++;
    return 1;
}
static int btt_identity(const char *buf, unsigned long n, const char *engine) {
    return bct_identity(buf, n, engine);
}
static int btt_thread(const char *buf, unsigned long n) { return bct_thread(buf, n); }
static unsigned btt_start(struct btt_state *s, int enabled, int identity, int thread, unsigned flags) {
    if (!enabled || !identity || !thread || (flags & 1u) || s->events >= 256 || s->next_id == ~0u) return 0;
    return ++s->next_id;
}
static int btt_event(struct btt_state *s, unsigned id) {
    if (!id || s->events >= 256) return 0;
    s->events++;
    return 1;
}
#endif
