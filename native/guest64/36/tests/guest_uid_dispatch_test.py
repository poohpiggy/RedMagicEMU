"""Exercise the actual API 36 guest UID syscall dispatch with Linux UID rules.

This is a host harness, not a claim that Binder's client-supplied identity is
authenticated. It deliberately extracts the real switch cases from run64.c.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
start = source.index("    case NR_getuid: return g_ruid;")
end = source.index("    case NR_rt_sigprocmask:", start)
cases = source[start:end]

program = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned long u64;
typedef unsigned int u32;
static int g_uid, g_gid, g_ruid, g_rgid, g_suid;
static int id_writes;
static void ids_put(void) { id_writes++; }
enum { NR_getuid=1, NR_geteuid, NR_getgid, NR_getegid, NR_setuid, NR_setgid,
       NR_setreuid, NR_setresuid, NR_setregid, NR_setresgid, NR_setfsuid,
       NR_setfsgid, NR_getresuid, NR_getresgid, NR_getgroups };
static long dispatch(int nr, u64 x, u64 y, u64 z) {
    u64 a[3] = {x,y,z};
    switch(nr) {
''' + cases + r'''
    default: return -999;
    }
}
static void reset(int r, int e, int s) { g_ruid=r; g_uid=e; g_suid=s; id_writes=0; }
int main(void) {
    const u64 nochange = (u32)-1;
    reset(12345,12345,12345);
    assert(dispatch(NR_setuid,0,0,0) == -EPERM);
    assert(g_ruid == 12345 && g_uid == 12345 && g_suid == 12345);
    assert(id_writes == 0);
    assert(dispatch(NR_setresuid,nochange,0,nochange) == -EPERM);
    assert(g_ruid == 12345 && g_uid == 12345 && g_suid == 12345);
    assert(id_writes == 0);
    reset(0,0,0);
    assert(dispatch(NR_setuid,12345,0,0) == 0);
    assert(g_ruid == 12345 && g_uid == 12345 && g_suid == 12345);
    assert(dispatch(NR_setuid,0,0,0) == -EPERM);
    reset(0,0,0);
    assert(dispatch(NR_setresuid,1000,1001,1002) == 0);
    assert(g_ruid == 1000 && g_uid == 1001 && g_suid == 1002);
    u32 real=0,effective=0,saved=0;
    assert(dispatch(NR_getresuid,(u64)&real,(u64)&effective,(u64)&saved) == 0);
    assert(real == 1000 && effective == 1001 && saved == 1002);
    assert(dispatch(NR_setresuid,1002,1000,1001) == 0);
    assert(g_ruid == 1002 && g_uid == 1000 && g_suid == 1001);
    assert(dispatch(NR_setresuid,42,nochange,nochange) == -EPERM);
    assert(g_ruid == 1002 && g_uid == 1000 && g_suid == 1001);
    reset(0,0,0);
    assert(dispatch(NR_setreuid,nochange,12345,0) == 0);
    assert(g_ruid == 0 && g_uid == 12345 && g_suid == 12345);
    assert(dispatch(NR_setuid,0,0,0) == 0);
    assert(g_ruid == 0 && g_uid == 0 && g_suid == 12345);
    reset(1000,1001,1002);
    assert(dispatch(NR_setreuid,1002,nochange,0) == -EPERM);
    assert(g_ruid == 1000 && g_uid == 1001 && g_suid == 1002);
    assert(dispatch(NR_setuid,nochange,0,0) == -EINVAL);
    reset(12345,12345,0);
    assert(dispatch(NR_setuid,0,0,0) == 0);
    assert(g_ruid == 12345 && g_uid == 0 && g_suid == 0);
    puts("GUEST_UID_DISPATCH_OK");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="rme-uid-dispatch-") as tmp:
    root = Path(tmp)
    cfile = root / "test.c"
    binary = root / "test"
    cfile.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-o", str(binary), str(cfile)], check=True)
    subprocess.run([str(binary)], check=True)
