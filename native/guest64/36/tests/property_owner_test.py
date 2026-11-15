"""Exercise the API 36 fstat owner translation on a synthetic property file.

The host harness compiles the actual kstat layout, fake_owner function and
fstat dispatch case from run64.c. A small adapter converts host fstat results
to the ARM64 layout; this is not an Android or full Guest execution test.
"""

from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
owner = source[source.index("struct kstat {"):source.index("// ---------------------------------------------------------------------------------------------------- ashmem")]
fstat_case = source[source.index("    case NR_fstat: {"):source.index("    case NR_faccessat: {")]
assert "fake_owner(st);" in fstat_case

harness = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
static u32 g_real_uid;
''' + owner + r'''
enum { NR_fstat = 80 };
static int g_logpipe;
static u64 g_logino, g_inino, g_logdev;
static int is_binder_fd(int fd) { (void)fd; return 0; }
static int is_ashmem_fd(int fd) { (void)fd; return 0; }
static long host_fstat(long nr, long fd, long ptr) {
    assert(nr == NR_fstat);
    struct stat s;
    if (fstat((int)fd, &s) != 0) return -1;
    struct kstat *out = (struct kstat *)ptr;
    memset(out, 0, sizeof *out);
    out->st_uid = (u32)s.st_uid; out->st_gid = (u32)s.st_gid;
    out->st_mode = (u32)s.st_mode; out->st_nlink = (u32)s.st_nlink;
    out->st_ino = (u64)s.st_ino; out->st_dev = (u64)s.st_dev;
    return 0;
}
#define S2(n, x, y) host_fstat((n), (long)(x), (long)(y))
static long guest_fstat(int fd, struct kstat *st) {
    long a[6] = { fd, (long)st, 0, 0, 0, 0 };
    long nr = NR_fstat;
    switch (nr) {
''' + fstat_case + r'''
    default: return -1;
    }
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int fd = open(argv[1], O_RDONLY);
    assert(fd >= 0);
    g_real_uid = (u32)getuid();
    struct stat real;
    struct kstat guest;
    assert(fstat(fd, &real) == 0);
    assert((u32)real.st_uid == g_real_uid);
    assert(guest_fstat(fd, &guest) == 0);
    assert(guest.st_uid == 0);
    assert((guest.st_mode & 0777) == 0644);
    if ((u32)real.st_gid == g_real_uid) assert(guest.st_gid == 0);
    struct kstat other = {0};
    other.st_uid = g_real_uid + 1; other.st_gid = g_real_uid + 1;
    other.st_mode = 0100600;
    fake_owner(&other);
    assert(other.st_uid == g_real_uid + 1 && other.st_gid == g_real_uid + 1);
    assert(close(fd) == 0);
    puts("PROPERTY_OWNER_FSTAT_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-property-owner-") as tmp:
    temp = Path(tmp)
    c_file = temp / "harness.c"
    binary = temp / "harness"
    prop_file = temp / "properties"
    c_file.write_text(harness, encoding="utf-8")
    prop_file.write_bytes(b"synthetic property area")
    prop_file.chmod(0o600)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(prop_file)], check=True)
