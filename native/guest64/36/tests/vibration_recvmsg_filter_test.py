"""Check actual run64 filter generation for the target-only recvmsg trap.

Runs the produced BPF in a tiny interpreter on Linux; it does not load Android
seccomp or run the original vendor service.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
syscalls = source[source.index("enum {\n    NR_setxattr"):source.index("\n#define AT_FDCWD")]
policy = source[source.index("struct bpf {", source.index("// ------------------------------------", source.index("static void on_sigsys"))):source.index("// ---------------------------------------------------------------------------------------------------- loader")]

harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
#define IMAGE_HI 0x20u
#define IMAGE_SPAN 0x00100000u
static int g_trace;
static int g_vibrator_preload;
static u32 tested_nr;
static long capture(void *program);
#define S5(...) ((void)0)
#define S3(n,a,b,c) capture((void *)(c))
static void die(const char *a, const char *b) { (void)a; (void)b; abort(); }
''' + syscalls + "\n" + policy + r'''
struct program { u16 length; struct bpf *filter; };
static u32 outcome;
static long capture(void *value) {
    struct program *p = value;
    if (p->length > 160) abort(); /* run64 allocates exactly 160 filters. */
    u32 accumulator = 0;
    for (u32 i = 0; i < p->length; ++i) {
        struct bpf ins = p->filter[i];
        if (ins.code == 0x20) {
            if (ins.k == 0) accumulator = tested_nr;
            else if (ins.k == 4) accumulator = 0xc00000b7u;
            else if (ins.k == 8 || ins.k == 12) accumulator = 0;
            else abort();
        } else if (ins.code == 0x15) i += accumulator == ins.k ? ins.jt : ins.jf;
        else if (ins.code == 0x35) i += accumulator >= ins.k ? ins.jt : ins.jf;
        else if (ins.code == 0x06) { outcome = ins.k; return 0; }
        else abort();
    }
    abort();
}
int main(void) {
    const u32 guarded[] = { NR_recvmsg, 243, 417, 425, 426, 427, 438 };
    for (unsigned i = 0; i < sizeof guarded / sizeof guarded[0]; ++i) {
        tested_nr = guarded[i];
        g_vibrator_preload = 0; install_filter();
        if (outcome != ALLOW) return 1;
        g_vibrator_preload = 1; install_filter();
        if (outcome != TRAP) return 2;
    }
    puts("VIBRATION_RECVMSG_FILTER_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-vibration-filter-") as temporary:
    temp = Path(temporary)
    (temp / "filter.c").write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(temp / "filter.c"), "-o", str(temp / "filter")], check=True)
    subprocess.run([str(temp / "filter")], check=True)
