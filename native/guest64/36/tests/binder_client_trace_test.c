#include <assert.h>
#include <stdio.h>
#include <string.h>

#if __has_include("../binder_client_trace.h")
#include "../binder_client_trace.h"
#else
static int bct_identity(const char *buf, unsigned long n, const char *engine) { (void)buf; (void)n; (void)engine; return 0; }
static int bct_thread(const char *buf, unsigned long n) { (void)buf; (void)n; return 0; }
static int bct_reserve(unsigned *count) { (void)count; return 0; }
static unsigned long bct_format(char *out, unsigned long p, unsigned long t, unsigned long f, int d, int phase) { (void)out; (void)p; (void)t; (void)f; (void)d; (void)phase; return 0; }
#endif

int main(void) {
    const char exact[] = "/app/libaemu64_36.so\0com.baidu.input_oem\0";
    const char child[] = "/app/libaemu64_36.so\0com.baidu.input_oem:media\0";
    const char foreign[] = "/app/other.so\0com.baidu.input_oem\0";
    assert(bct_identity(exact, sizeof exact - 1, "/app/libaemu64_36.so"));
    assert(!bct_identity(child, sizeof child - 1, "/app/libaemu64_36.so"));
    assert(!bct_identity(foreign, sizeof foreign - 1, "/app/libaemu64_36.so"));
    assert(!bct_identity(exact, sizeof exact - 3, "/app/libaemu64_36.so"));
    assert(bct_thread("fin-1-thread-4\n", 15));
    assert(!bct_thread("fin-1-thread-40\n", 16));
    assert(!bct_thread("fin-1-thread-4x", 15));
    unsigned count = 0;
    for (unsigned i = 0; i < 128; i++) assert(bct_reserve(&count));
    assert(!bct_reserve(&count));
    char line[160] = {0};
    unsigned long n = bct_format(line, 42, 43, 261, 1, 0);
    const char expected[] = "binder-client pid=42 tid=43 fd=261 driver=main phase=wait\n";
    assert(n == sizeof expected - 1 && memcmp(line, expected, n) == 0);
    memset(line, 0, sizeof line);
    n = bct_format(line, 42, 43, 261, 1, 1);
    const char received[] = "binder-client pid=42 tid=43 fd=261 driver=main phase=recv\n";
    assert(n == sizeof received - 1 && memcmp(line, received, n) == 0);
    memset(line, 0, sizeof line);
    n = bct_format(line, 42, 43, 261, 8, 9);
    const char fallback[] = "binder-client pid=42 tid=43 fd=261 driver=unknown phase=error\n";
    assert(n == sizeof fallback - 1 && memcmp(line, fallback, n) == 0);
    puts("binder client trace gating passed");
    return 0;
}
