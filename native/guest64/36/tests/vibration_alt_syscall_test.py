"""Check run64's actual target-only alternate FD-acquisition cases.

The normal path is a stub in this Linux test. It does not execute Android or
prove that these are every possible FD-acquisition syscall.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
syscalls = source[source.index("enum {\n    NR_setxattr"):source.index("\n#define AT_FDCWD")]
start = source.index("    case NR_recvmmsg: case NR_recvmmsg_time64:")
cases = source[start:source.index("    case NR_rt_sigaction:", start)]

harness = r'''
#include <stdio.h>
#include <stdlib.h>
static int g_vibrator_preload;
static int forwarded;
static long forward(long nr, long a, long b, long c, long d, long e, long f) {
    (void)nr; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    ++forwarded; return 73;
}
#define S6(n,a,b,c,d,e,f) forward(n,a,b,c,d,e,f)
''' + syscalls + r'''
static long invoke(long nr, long *a) { switch (nr) {
''' + cases + r'''
default: abort();
} }
int main(void) {
    const long blocked[] = { NR_recvmmsg, NR_recvmmsg_time64, NR_io_uring_setup,
        NR_io_uring_enter, NR_io_uring_register, NR_pidfd_getfd };
    long arguments[6] = {0};
    for (unsigned i = 0; i < sizeof blocked / sizeof blocked[0]; ++i) {
        forwarded = 0; g_vibrator_preload = 1;
        if (invoke(blocked[i], arguments) != -1 || forwarded) return 1;
        g_vibrator_preload = 0;
        if (invoke(blocked[i], arguments) != 73 || forwarded != 1) return 2;
    }
    puts("VIBRATION_ALT_SYSCALL_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-vibration-alt-syscall-") as temporary:
    temp = Path(temporary)
    (temp / "cases.c").write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(temp / "cases.c"), "-o", str(temp / "cases")], check=True)
    subprocess.run([str(temp / "cases")], check=True)
