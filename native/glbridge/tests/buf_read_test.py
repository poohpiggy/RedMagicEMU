#!/usr/bin/env python3
"""Compile and exercise host.c's actual buf_read with controlled dependencies."""

from pathlib import Path
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parents[1] / "host.c"


def production_function():
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("static void buf_read(")
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise RuntimeError("unterminated buf_read")


HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EGL_NO_SYNC_KHR NULL
#define EGL_CONDITION_SATISFIED_KHR 0x30F6
#define EGL_TIMEOUT_EXPIRED_KHR 0x30F5
#define AHARDWAREBUFFER_USAGE_CPU_READ_RARELY 2
typedef struct { uint32_t stride; } AHardwareBuffer_Desc;
struct hbuf { uint64_t id, frame; int gpu, w, h; void *sync, *ahb; };
struct conn { unsigned char *px; size_t px_cap; };
static struct hbuf g_buf[512];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static struct conn conn;
static struct conn *C = &conn;
static void *g_dpy;
static int status, payload_bytes, lock_calls, unlock_calls, fence_calls, wait_calls, wait_mode, fence_result;
static int lock_returns_null, unlock_result;
static unsigned char pixels[2][12] = {{1,2,3,4,5,6,7,8,9,9,9,9}, {11,12,13,14,15,16,17,18,9,9,9,9}};

static int test_cond_wait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) {
    (void)c; (void)m; (void)t;
    wait_calls++;
    if (wait_mode == 1 && wait_calls == 3) { g_buf[0].frame = 7; return 0; }
    return wait_calls < 3 ? 0 : ETIMEDOUT;
}
#define pthread_cond_timedwait test_cond_wait
static int eglClientWaitSyncKHR(void *d, void *s, int flags, long long timeout) {
    (void)d; (void)s; (void)flags; (void)timeout; fence_calls++; return fence_result;
}
static void AHardwareBuffer_describe(void *b, AHardwareBuffer_Desc *d) { (void)b; d->stride = 3; }
static int AHardwareBuffer_lock(void *b, int usage, int fence, void *rect, void **va) {
    (void)b; (void)usage; (void)fence; (void)rect; lock_calls++; *va = lock_returns_null ? NULL : pixels; return 0;
}
static int AHardwareBuffer_unlock(void *b, void *f) { (void)b; (void)f; unlock_calls++; return unlock_result; }
static void reply32(uint32_t v) { status = (int)v; }
static void reply_flush(void) {}
static void wr_all(const void *p, size_t n) {
    const unsigned char expected[] = {1,2,3,4,5,6,7,8,11,12,13,14,15,16,17,18};
    assert(n == sizeof expected && memcmp(p, expected, n) == 0);
    payload_bytes += (int)n;
}
'''

CASES = r'''
static void reset_case(void) {
    memset(g_buf, 0, sizeof g_buf);
    g_buf[0] = (struct hbuf){.id=42, .frame=6, .gpu=1, .w=2, .h=2, .sync=(void*)1, .ahb=(void*)2};
    status = -1; payload_bytes = lock_calls = unlock_calls = fence_calls = wait_calls = wait_mode = 0;
    lock_returns_null = unlock_result = 0;
    fence_result = EGL_CONDITION_SATISFIED_KHR;
}
static int require(int ok, const char *what) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", what);
    return ok;
}
int main(void) {
    int ok = 1;
    reset_case();
    buf_read(42, 7, 2, 2);
    ok &= require(wait_calls == 3 && status == 0 && !payload_bytes && !lock_calls && !fence_calls && g_buf[0].gpu,
                  "timeout after spurious wakes must not return the earlier frame");

    reset_case(); wait_mode = 1;
    buf_read(42, 7, 2, 2);
    ok &= require(wait_calls == 3 && status == 1 && payload_bytes == 16 && lock_calls == 1 && !g_buf[0].gpu,
                  "frame arriving after spurious wakes must be returned");

    reset_case(); g_buf[0].frame = 7;
    buf_read(42, 7, 2, 2);
    ok &= require(!wait_calls && status == 1 && payload_bytes == 16 && fence_calls == 1,
                  "already published frame with signalled fence must be returned");

    reset_case(); g_buf[0].frame = 7; g_buf[0].sync = EGL_NO_SYNC_KHR;
    buf_read(42, 7, 2, 2);
    ok &= require(status == 1 && payload_bytes == 16 && !fence_calls,
                  "published frame without a fence must keep existing behavior");

    reset_case(); g_buf[0].frame = 7; fence_result = EGL_TIMEOUT_EXPIRED_KHR;
    buf_read(42, 7, 2, 2);
    ok &= require(status == 0 && !payload_bytes && !lock_calls && g_buf[0].gpu,
                  "unsignalled fence must not return pixels or clear GPU ownership");

    reset_case(); g_buf[0].frame = 7; fence_result = 0;
    buf_read(42, 7, 2, 2);
    ok &= require(status == 0 && !payload_bytes && !lock_calls && g_buf[0].gpu,
                  "failed fence wait must not return pixels or clear GPU ownership");

    reset_case(); g_buf[0].frame = 7; unlock_result = -1;
    buf_read(42, 7, 2, 2);
    ok &= require(status == 0 && !payload_bytes && lock_calls == 1 && unlock_calls == 1 && g_buf[0].gpu,
                  "failed buffer unlock must not send pixels or clear GPU ownership");

    reset_case(); g_buf[0].frame = 7; lock_returns_null = 1;
    buf_read(42, 7, 2, 2);
    ok &= require(status == 0 && !payload_bytes && lock_calls == 1 && unlock_calls == 1 && g_buf[0].gpu,
                  "successful buffer lock with null address must still be unlocked");


    free(conn.px);
    if (ok) puts("buf_read frame and fence checks: OK");
    return ok ? 0 : 1;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="rme-buf-read-test-") as directory:
        source = Path(directory) / "buf_read_test.c"
        binary = Path(directory) / "buf_read_test"
        source.write_text(HARNESS + "\n" + production_function() + "\n" + CASES, encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
