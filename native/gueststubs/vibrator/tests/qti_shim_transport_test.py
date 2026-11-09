"""QTI-shaped PLT caller -> shim -> real adapter/sender -> host frame gate.

Uses one Linux test process and a socketpair. It does not run the original
Android binary, prove its linker behaviour, authorize Guest apps, or vibrate.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
repo = root.parents[2]
vendor = r'''
int qti_write(void *, const char *, char *)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
int qti_write(void *self, const char *path, char *value) {
    (void)self; (void)path; (void)value; return -99;
}
int vendor_write(const char *path, char *value) { return qti_write(0, path, value); }
'''
runner = r'''
#define _GNU_SOURCE
#include "qti_led_adapter.h"
#include "qti_led_shim.h"
#include "vibration_host_gate.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

int vendor_write(const char *, char *);
#define BASE "/sys/class/leds/zte_vibrator/"

int main(void) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    int one = 1;
    assert(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) == 0);
    int pidfd = (int)syscall(SYS_pidfd_open, getpid(), 0);
    if (pidfd < 0) { puts("QTI_SHIM_TRANSPORT_SKIP_NO_PIDFD"); return 77; }
    int (*sender_init)(struct rme_guest_vibration_sender *, int, uint64_t) = dlsym(RTLD_DEFAULT, "rme_guest_vibration_init");
    int (*sender_close)(struct rme_guest_vibration_sender *) = dlsym(RTLD_DEFAULT, "rme_guest_vibration_close");
    int (*sender_destroy)(struct rme_guest_vibration_sender *) = dlsym(RTLD_DEFAULT, "rme_guest_vibration_destroy");
    int (*adapter_init)(struct rme_qti_led_adapter *, struct rme_guest_vibration_sender *) = dlsym(RTLD_DEFAULT, "rme_qti_led_init");
    int (*adapter_destroy)(struct rme_qti_led_adapter *) = dlsym(RTLD_DEFAULT, "rme_qti_led_destroy");
    int (*bind)(struct rme_qti_led_adapter *) = dlsym(RTLD_DEFAULT, "rme_qti_shim_bind");
    int (*unbind)(struct rme_qti_led_adapter *) = dlsym(RTLD_DEFAULT, "rme_qti_shim_unbind");
    assert(sender_init && sender_close && sender_destroy && adapter_init && adapter_destroy && bind && unbind);
    struct rme_guest_vibration_sender sender;
    struct rme_qti_led_adapter adapter;
    assert(sender_init(&sender, pair[0], 123) == 0);
    assert(adapter_init(&adapter, &sender) == 0);
    assert(bind(&adapter) == 0);
    assert(vendor_write(BASE "gain", "0x32") == 0);
    assert(vendor_write(BASE "activate_mode", "5") == 0);
    assert(vendor_write(BASE "index", "4") == 0);
    assert(vendor_write(BASE "duration_aw", "500\n") == 0);
    assert(rme_host_read_vibration(pair[1], pidfd, getpid(), getuid(), 123, 1) == 0);
    assert(vendor_write(BASE "activate_aw", "1") == 0);
    assert(rme_host_read_vibration(pair[1], pidfd, getpid(), getuid(), 123, 1) == 501);
    assert(vendor_write(BASE "activate_aw", "0") == 0);
    assert(rme_host_read_vibration(pair[1], pidfd, getpid(), getuid(), 123, 2) == 1);
    assert(unbind(&adapter) == 0);
    assert(vendor_write(BASE "activate_aw", "1") < 0);
    assert(adapter_destroy(&adapter) == 0);
    assert(sender_close(&sender) == 0);
    assert(sender_destroy(&sender) == 0);
    close(pair[1]); close(pidfd);
    puts("QTI_SHIM_TRANSPORT_OK");
}
'''
with tempfile.TemporaryDirectory(prefix="rme-qti-chain-") as tmp:
    tmp = Path(tmp)
    (tmp / "vendor.c").write_text(vendor, encoding="utf-8")
    (tmp / "runner.c").write_text(runner, encoding="utf-8")
    cc = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    subprocess.run(cc + ["-shared", "-fPIC", str(tmp / "vendor.c"), "-o", str(tmp / "vendor.so")], check=True)
    subprocess.run(cc + ["-shared", "-fPIC", "-pthread", str(root / "qti_led_shim.c"), str(root / "qti_led_adapter.c"), str(root / "guest_sender.c"), "-o", str(tmp / "shim.so")], check=True)
    subprocess.run(cc + ["-I" + str(root), "-I" + str(repo / "native/hostjni"), str(tmp / "runner.c"), str(tmp / "vendor.so"), str(repo / "native/hostjni/vibration_host_gate.c"), str(repo / "native/gueststubs/radio-bridge/gate.c"), str(repo / "native/gueststubs/radio-bridge/receive.c"), "-ldl", "-Wl,-rpath," + str(tmp), "-o", str(tmp / "runner")], check=True)
    env = os.environ.copy()
    env["LD_PRELOAD"] = str(tmp / "shim.so")
    subprocess.run([str(tmp / "runner")], env=env, check=True)
