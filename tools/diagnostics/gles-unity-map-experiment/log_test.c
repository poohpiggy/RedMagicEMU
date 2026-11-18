/* Exercise actual guest.c and capture its Android log without a device. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "../../../native/glbridge/guest.c"

enum { FAKE_FD = 314159, COPY_WRITE = 0x8f37 };
static struct ts stream;
static char last_log[1024];
static int reject_logs;
static int fail_malloc, fail_calloc;
void *__real_malloc(size_t n);
void *__wrap_malloc(size_t n) { return fail_malloc ? NULL : __real_malloc(n); }
void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size) { return fail_calloc ? NULL : __real_calloc(n, size); }
int __android_log_print(int priority, const char *tag, const char *fmt, ...) {
    (void)priority; (void)tag;
    char msg[1024];
    va_list args;
    va_start(args, fmt); vsnprintf(msg, sizeof(msg), fmt, args); va_end(args);
    if (strstr(msg, "unity_map_reject")) { ++reject_logs; snprintf(last_log, sizeof(last_log), "%s", msg); }
    return 0;
}
int __system_property_get(const char *name, char *value) {
    if (!strcmp(name, "debug.aemu.gl")) { value[0] = '3'; value[1] = 0; return 1; }
    value[0] = 0; return 0;
}
ssize_t __real_write(int fd, const void *buf, size_t len);
ssize_t __wrap_write(int fd, const void *buf, size_t len) {
    return fd == FAKE_FD ? (ssize_t)len : __real_write(fd, buf, len);
}
ssize_t __real_read(int fd, void *buf, size_t len);
ssize_t __wrap_read(int fd, void *buf, size_t len) {
    if (fd != FAKE_FD) return __real_read(fd, buf, len);
    if (len != 4) { errno = EIO; return -1; }
    uint32_t id = 101; memcpy(buf, &id, 4); return 4;
}
static void *map(long off, long len, unsigned flags) { return e_glMapBufferRange(COPY_WRITE, off, len, flags); }
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    stream.fd = FAKE_FD; stream.cap = 131072; stream.buf = calloc(stream.cap, 1);
    if (!stream.buf) return 2;
    t_ts = &stream;
    const EGLint attrs[] = { 0x3098, 3, 0x3038 };
    struct ectx *c = eglCreateContext(NULL, NULL, NULL, attrs);
    if (!c || !c->bridged) return 2;
    stream.ctx = c;
    const char *reason = argv[1];
    long off = 0, len = 8; unsigned flags = 0x36;
    unsigned expected_name = !strcmp(reason, "unbound") ? 0 : 1;
    struct unity_maps *saved_group = NULL;
    if (strcmp(reason, "unbound")) e_glBindBuffer(COPY_WRITE, 1);
    if (!strcmp(reason, "invalid_offset")) off = -1;
    else if (!strcmp(reason, "invalid_length")) len = 0;
    else if (!strcmp(reason, "length_limit")) len = (64L << 20) + 1;
    else if (!strcmp(reason, "range_overflow")) { off = LONG_MAX - 2; len = 8; }
    else if (!strcmp(reason, "disconnected")) stream.dead = 1;
    else if (!strcmp(reason, "no_group")) { saved_group = c->unity_maps; c->unity_maps = NULL; }
    else if (!strcmp(reason, "allocation_failed")) fail_calloc = 1;
    else if (!strcmp(reason, "legacy_allocation_failed")) { flags = 2; fail_malloc = 1; }
    else if (!strcmp(reason, "binding_unknown")) { GLuint n = 1; e_glDeleteBuffers(1, &n); }
    else if (!strcmp(reason, "name_mapped")) { if (!map(0, 8, flags)) return 2; }
    else if (!strcmp(reason, "retired_name")) {
        if (!map(0, 8, flags)) return 2;
        GLuint n = 1; e_glDeleteBuffers(1, &n); e_glBindBuffer(COPY_WRITE, n);
    } else if (!strcmp(reason, "slot_exhausted")) {
        for (unsigned i = 1; i <= 8; ++i) { e_glBindBuffer(COPY_WRITE, i); if (!map(0, 8, flags)) return 2; }
        e_glBindBuffer(COPY_WRITE, 9); expected_name = 9;
    } else if (!strcmp(reason, "unsupported_flags_busy")) {
        if (!map(0, 8, flags)) return 2;
        e_glBindBuffer(COPY_WRITE, 2); flags = 2; expected_name = 2;
    } else if (!strcmp(reason, "legacy_mapping") || !strcmp(reason, "legacy_map_unavailable")) {
        c->g.maps[0].target = COPY_WRITE; c->g.maps[0].ptr = malloc(8);
        if (!c->g.maps[0].ptr) return 2;
        if (!strcmp(reason, "legacy_map_unavailable")) flags = 2;
    }
    reject_logs = 0; last_log[0] = 0;
    if (map(off, len, flags) || map(off, len, flags)) { fprintf(stderr, "unexpected successful map\n"); return 1; }
    char expected[256];
    snprintf(expected, sizeof(expected), "unity_map_reject reason=%s buffer=%u offset=%ld length=%ld flags=0x%x",
             reason, expected_name, off, len, flags);
    int ok = reject_logs == 1 && !strcmp(last_log, expected);
    if (!ok) fprintf(stderr, "expected one %s diagnostic, saw %d: %s\n", expected, reject_logs, last_log);
    fail_malloc = fail_calloc = 0;
    if (saved_group) c->unity_maps = saved_group;
    stream.dead = 0; eglDestroyContext(NULL, c); t_ts = NULL; free(stream.buf);
    return ok ? 0 : 1;
}
