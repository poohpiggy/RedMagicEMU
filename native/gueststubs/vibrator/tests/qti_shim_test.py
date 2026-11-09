"""Exercise the QTI interception symbol through a real ELF PLT on Linux.

The vendor and adapter below are test doubles; Android linker behaviour,
Guest authorization and physical hardware are not tested here.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
vendor = r'''
static int originals;
int original(void *self, const char *path, char *value)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
int original(void *self, const char *path, char *value) {
    (void)self; (void)path; (void)value; ++originals; return 42;
}
int vendor_write(const char *path, char *value) { return original(0, path, value); }
int vendor_original_count(void) { return originals; }
'''
stub = r'''
#include <errno.h>
#include <stddef.h>
#include <string.h>
struct rme_qti_led_adapter;
static unsigned count;
int rme_qti_led_write(struct rme_qti_led_adapter *adapter, const char *path, const void *value, size_t size) {
    if (adapter != (void *)0x1 || strcmp(path, "/sys/class/leds/zte_vibrator/activate_aw") ||
        size != 2 || memcmp(value, "1", 2)) return -EINVAL;
    count++;
    return 0;
}
unsigned stub_count(void) { return count; }
'''
runner = r'''
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
int vendor_write(const char *, char *);
int vendor_original_count(void);
typedef int (*bind_fn)(void *);
typedef int (*unbind_fn)(void *);
typedef unsigned (*count_fn)(void);
int main(void) {
    const char *path = "/sys/class/leds/zte_vibrator/activate_aw";
    bind_fn bind = (bind_fn)dlsym(RTLD_DEFAULT, "rme_qti_shim_bind");
    unbind_fn unbind = (unbind_fn)dlsym(RTLD_DEFAULT, "rme_qti_shim_unbind");
    count_fn count = (count_fn)dlsym(RTLD_DEFAULT, "stub_count");
    assert(bind && unbind && count);
    assert(vendor_write(path, "1") < 0 && count() == 0);
    assert(bind((void *)0x1) == 0);
    assert(vendor_write(path, "1") == 0 && count() == 1);
    assert(bind((void *)0x1) < 0);
    assert(vendor_write("/sys/class/leds/other/activate_aw", "1") < 0 && count() == 1);
    assert(vendor_original_count() == 0);
    assert(unbind((void *)0x2) < 0);
    assert(unbind((void *)0x1) == 0);
    assert(vendor_write(path, "1") < 0 && count() == 1);
    assert(bind((void *)0x1) < 0);
    puts("QTI_SHIM_LINUX_PLT_OK");
}
'''
with tempfile.TemporaryDirectory(prefix="rme-qti-shim-") as tmp:
    tmp = Path(tmp)
    for name, content in (("vendor.c", vendor), ("stub.c", stub), ("runner.c", runner)):
        (tmp / name).write_text(content, encoding="utf-8")
    cc = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    subprocess.run(cc + ["-shared", "-fPIC", str(tmp / "vendor.c"), "-o", str(tmp / "vendor.so")], check=True)
    subprocess.run(cc + ["-shared", "-fPIC", "-pthread", str(root / "qti_led_shim.c"), str(tmp / "stub.c"), "-o", str(tmp / "shim.so")], check=True)
    subprocess.run(cc + [str(tmp / "runner.c"), str(tmp / "vendor.so"), "-ldl", "-Wl,-rpath," + str(tmp), "-o", str(tmp / "runner")], check=True)
    env = os.environ.copy()
    env["LD_PRELOAD"] = str(tmp / "shim.so")
    subprocess.run([str(tmp / "runner")], env=env, check=True)
