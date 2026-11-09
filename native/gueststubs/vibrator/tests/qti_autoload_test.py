"""Linux preloaded fake vendor -> real session. No Android or physical motor."""
from pathlib import Path
import os
import socket
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
vendor = r'''
int original(void *, const char *, char *)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
int original(void *self, const char *path, char *value) {
    (void)self; (void)path; (void)value; return -99;
}
int vendor_write(const char *path, char *value) { return original(0, path, value); }
'''
runner = r'''
#include <stdio.h>
#include <string.h>
int vendor_write(const char *, char *);
#define BASE "/sys/class/leds/zte_vibrator/"
int main(int argc, char **argv) {
    int first = vendor_write(BASE "gain", "0x32");
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "reject")) return first < 0 ? 0 : 1;
    if (first || vendor_write(BASE "activate_mode", "5") || vendor_write(BASE "index", "4") ||
        vendor_write(BASE "duration_aw", "500\n") || vendor_write(BASE "activate_aw", "1")) return 1;
    if (!strcmp(argv[1], "wait") && getchar() == EOF) return 1;
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="rme-qti-autoload-") as directory:
    tmp = Path(directory)
    (tmp / "vendor.c").write_text(vendor, encoding="utf-8")
    (tmp / "runner.c").write_text(runner, encoding="utf-8")
    cc = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    subprocess.run(cc + ["-shared", "-fPIC", str(tmp / "vendor.c"), "-o", str(tmp / "vendor.so")], check=True)
    source = ["qti_led_shim.c", "qti_led_adapter.c", "guest_sender.c", "qti_session.c"]
    if (root / "qti_autoload.c").exists():
        source.append("qti_autoload.c")
    subprocess.run(cc + ["-shared", "-fPIC", "-pthread"] + [str(root / n) for n in source] + ["-o", str(tmp / "shim.so")], check=True)
    subprocess.run(cc + [str(tmp / "runner.c"), str(tmp / "vendor.so"), "-Wl,-rpath," + str(tmp), "-o", str(tmp / "runner")], check=True)
    env = os.environ.copy()
    env["LD_PRELOAD"] = str(tmp / "shim.so")
    for key in ("RME_VIBRATION_SOCKET", "RME_VIBRATION_HOST_PID", "RME_VIBRATION_HOST_UID", "RME_VIBRATION_EPOCH"):
        env.pop(key, None)
    subprocess.run([str(tmp / "runner"), "reject"], env=env, check=True, timeout=5)
    name = "rme-autoload-" + str(os.getpid())
    server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    server.bind("\0" + name)
    server.listen(1)
    server.settimeout(4)
    env.update(RME_VIBRATION_SOCKET=name, RME_VIBRATION_HOST_PID=str(os.getpid()),
               RME_VIBRATION_HOST_UID=str(os.getuid()), RME_VIBRATION_EPOCH="123")
    for key, bad in (("RME_VIBRATION_HOST_PID", "0"), ("RME_VIBRATION_HOST_UID", "-1"),
                     ("RME_VIBRATION_HOST_UID", "4294967296"),
                     ("RME_VIBRATION_EPOCH", "9223372036854775808"), ("RME_VIBRATION_EPOCH", "123x")):
        invalid = dict(env)
        invalid[key] = bad
        result = subprocess.run([str(tmp / "runner"), "reject"], env=invalid, check=True, timeout=5, capture_output=True)
        assert result.stderr == b"vibration: invalid session parameters\n"
    server.settimeout(0.1)
    try:
        server.accept()
        raise AssertionError("invalid parameters connected")
    except TimeoutError:
        pass
    server.settimeout(4)
    child = subprocess.Popen([str(tmp / "runner"), "reject"], env=env, stderr=subprocess.PIPE)
    try:
        conn, _ = server.accept()
        conn.settimeout(4)
        conn.sendall(b"RMEH" + (124).to_bytes(8, "big"))
        assert conn.recv(28) == b""
        _, stderr = child.communicate(timeout=4)
        assert child.returncode == 0 and stderr == b"vibration: session start failed\n"
        conn.close()
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    child = subprocess.Popen([str(tmp / "runner"), "wait"], env=env, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        conn, _ = server.accept()
        conn.settimeout(4)
        conn.sendall(b"RMEH" + (123).to_bytes(8, "big"))
        on = conn.recv(28)
        assert on[:4] == b"RME1" and on[23] == 1
        conn.close()
        _, stderr = child.communicate(input=b"\n", timeout=4)
        assert child.returncode == 0 and stderr == b"vibration: session close failed\n"
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    child = subprocess.Popen([str(tmp / "runner"), "on"], env=env)
    try:
        conn, _ = server.accept()
        conn.settimeout(4)
        conn.sendall(b"RMEH" + (123).to_bytes(8, "big"))
        on, off = conn.recv(28), conn.recv(28)
        assert on[:4] == b"RME1" and on[23] == 1 and int.from_bytes(on[24:28], "big") == 500
        assert off[:4] == b"RME1" and off[23] == 2 and off[24:28] == b"\0" * 4
        assert child.wait(timeout=4) == 0
        conn.close()
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        server.close()
print("QTI_AUTOLOAD_OK")
