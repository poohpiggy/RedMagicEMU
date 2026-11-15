"""Exercise the API 36 engine's actual setsockopt dispatch on a host fixture.

This test does not load seccomp or open a network socket. It verifies syscall
return values, including preserving the existing EPERM compatibility fallback.
It does not prove that a guest SO_MARK binds a socket to the intended network.
Run under Linux (including WSL): python3 native/guest64/36/tests/sock_mark_error_test.py
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
start = source.index("    case NR_setsockopt: {")
end = source.index("    case NR_getsockopt:", start)
dispatch_case = source[start:end]

program = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned long u64;
enum { NR_setsockopt = 1 };
static long kernel_result;
static int netlink;
static int is_nl_fd(int fd) { (void)fd; return netlink; }
static long fake_syscall(long nr, u64 fd, u64 level, u64 option, u64 value, u64 length) {
    (void)nr; (void)fd; (void)level; (void)option; (void)value; (void)length;
    return kernel_result;
}
#define S5(nr,a,b,c,d,e) fake_syscall(nr,a,b,c,d,e)
static long dispatch(u64 level, u64 option, long result, int is_netlink) {
    u64 a[] = { 3, level, option, 0, 4 };
    const int nr = NR_setsockopt;
    kernel_result = result;
    netlink = is_netlink;
    switch (nr) {
''' + dispatch_case + r'''
    default: return -999;
    }
}
int main(void) {
    /* Permission failure remains compatible with existing guest startup. */
    assert(dispatch(1, 36, -EPERM, 0) == 0);
    assert(dispatch(1, 36, 0, 0) == 0);
    /* Bad descriptors/arguments, policy denial, and unsupported options are
     * not proof of a missing capability: never report those as success. */
    assert(dispatch(1, 36, -EBADF, 0) == -EBADF);
    assert(dispatch(1, 36, -EINVAL, 0) == -EINVAL);
    assert(dispatch(1, 36, -EACCES, 0) == -EACCES);
    assert(dispatch(1, 36, -ENOPROTOOPT, 0) == -ENOPROTOOPT);
    assert(dispatch(1, 7, -EPERM, 0) == -EPERM);
    /* Fake netlink options retain their pre-existing handling. */
    assert(dispatch(1, 36, -EPERM, 1) == 0);
    assert(dispatch(1, 20, -EINVAL, 1) == -EINVAL);
    puts("SO_MARK_ERROR_DISPATCH_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-so-mark-test-") as temporary:
    root = Path(temporary)
    cfile, binary = root / "test.c", root / "test"
    cfile.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(cfile),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
