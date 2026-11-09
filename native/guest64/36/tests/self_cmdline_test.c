#include <assert.h>
#include <string.h>
#include <stdio.h>
#if __has_include("../self_cmdline.h")
#include "../self_cmdline.h"
#else
static int cmdline_matches_self(const char *path, unsigned long pid) { (void)path; (void)pid; return 0; }
static unsigned long cmdline_guest_offset(const char *data, unsigned long len, const char *engine) { (void)data; (void)len; (void)engine; return 0; }
#endif
int main(void) {
    const char sample[] = "/app/libaemu64_36.so\0com.baidu.input_oem\0--arg\0";
    assert(cmdline_matches_self("/proc/1234/cmdline", 1234));
    assert(!cmdline_matches_self("/proc/12345/cmdline", 1234));
    assert(cmdline_matches_self("/proc/self/cmdline", 1234));
    assert(!cmdline_matches_self("/proc/self/cmdline/extra", 1234));
    assert(!cmdline_matches_self("/proc/selfish/cmdline", 1234));
    assert(!cmdline_matches_self("/proc/1234/cmdline/extra", 1234));
    unsigned long off = cmdline_guest_offset(sample, sizeof sample - 1, "/app/libaemu64_36.so");
    assert(off == sizeof "/app/libaemu64_36.so");
    assert(memcmp(sample + off, "com.baidu.input_oem\0--arg\0", sizeof "com.baidu.input_oem\0--arg\0" - 1) == 0);
    assert(cmdline_guest_offset(sample, 5, "/app/libaemu64_36.so") == 0);
    assert(cmdline_guest_offset(sample, sizeof sample - 1, "/other") == 0);
    assert(cmdline_guest_offset("/app/libaemu64_36.so\0", sizeof "/app/libaemu64_36.so", "/app/libaemu64_36.so") == 0);
    puts("self cmdline tests passed");
}
