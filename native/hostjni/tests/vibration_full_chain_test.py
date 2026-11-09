"""Offline launcher -> fake Guest QTI calls -> actual vibration host gate.

Everything is compiled into a temporary directory. The Guest is a self-created
static Linux executable, not the original Android HAL. This does not prove
Android linker behaviour, app authorization, complete isolation, or vibration.
"""

from pathlib import Path
import ctypes
import errno
import os
import platform
import select
import socket
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
GUEST_SOURCE = r'''
#include "qti_session.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int original_write(void *, const char *, char *)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
#define BASE "/sys/class/leds/zte_vibrator/"
#define WRITE(node, value) do { if (original_write(0, BASE node, value) != 0) return 4; } while (0)

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    struct rme_qti_session session = {0};
    if (rme_qti_session_start(&session, argv[1], (pid_t)strtol(argv[2], 0, 10),
                              (uid_t)strtoul(argv[3], 0, 10), 123) != 0) return 3;
    WRITE("gain", "0x32");
    WRITE("activate_mode", "5");
    WRITE("index", "4");
    WRITE("duration_aw", "500\n");
    WRITE("activate_aw", "1");
    WRITE("activate_aw", "0");
    if (rme_qti_session_close(&session) != 0) return 5;
    /* Keep the pinned process alive until the test receives queued frames. */
    if (write(STDOUT_FILENO, "D", 1) != 1) return 6;
    char release;
    return read(STDIN_FILENO, &release, 1) == 1 && release == 'R' ? 0 : 7;
}
'''


def read_exact(sock, length):
    result = bytearray()
    while len(result) < length:
        part = sock.recv(length - len(result))
        if not part:
            raise AssertionError("launcher closed during handshake")
        result.extend(part)
    return bytes(result)


def peer_is_process(sock, process):
    pid, uid, _ = struct.unpack("3i", sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    assert pid == process.pid and uid == os.getuid(), (pid, uid, process.pid)


def main():
    if platform.machine() not in ("x86_64", "aarch64"):
        print("VIBRATION_FULL_CHAIN_SKIPPED_ARCH")
        return 77
    if not hasattr(os, "pidfd_open"):
        print("VIBRATION_FULL_CHAIN_SKIPPED_PIDFD")
        return 77
    try:
        probe = os.pidfd_open(os.getpid(), 0)
    except OSError as error:
        if error.errno in (errno.ENOSYS, errno.EPERM, errno.EACCES):
            print("VIBRATION_FULL_CHAIN_SKIPPED_PIDFD")
            return 77
        raise
    os.close(probe)
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.syscall(444, 0, 0, 1) < 5:
        print("VIBRATION_FULL_CHAIN_SKIPPED_LANDLOCK")
        return 77
    with tempfile.TemporaryDirectory(prefix="rme-vibration-chain-") as temp:
        temp = Path(temp)
        guest_root = temp / "guest"
        guest_root.mkdir()
        guest_c = temp / "guest.c"
        guest_c.write_text(GUEST_SOURCE)
        guest = temp / "guest-program"
        launcher = temp / "launcher"
        gate = temp / "gate.so"
        native = ROOT / "native"
        vib = native / "gueststubs/vibrator"
        cc = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror"]
        subprocess.run(cc + ["-static", "-pthread", "-I" + str(vib), str(guest_c),
                             *[str(vib / x) for x in ("qti_session.c", "qti_led_shim.c", "qti_led_adapter.c", "guest_sender.c")],
                             "-o", str(guest)], check=True)
        subprocess.run(cc + [str(native / "hostjni/guest_launch.c"),
                             str(native / "hostjni/vibration_metadata_guard.c"), "-o", str(launcher)], check=True)
        subprocess.run(cc + ["-fPIC", "-shared", str(native / "hostjni/vibration_host_gate.c"),
                             str(native / "gueststubs/radio-bridge/gate.c"),
                             str(native / "gueststubs/radio-bridge/receive.c"), "-o", str(gate)], check=True)
        read_frame = ctypes.CDLL(str(gate)).rme_host_read_vibration
        read_frame.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int32, ctypes.c_uint32,
                               ctypes.c_uint64, ctypes.c_uint64]
        read_frame.restype = ctypes.c_int
        control_name = f"rme-vib-full-launch-{os.getpid()}"
        vib_name = f"rme-vib-full-events-{os.getpid()}"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as control, \
             socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as events:
            control.settimeout(8)
            control.bind("\0" + control_name)
            control.listen(1)
            events.settimeout(8)
            events.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
            events.bind("\0" + vib_name)
            events.listen(1)
            proc = subprocess.Popen([str(launcher), control_name, "--experimental-vibration-guard",
                                     str(guest_root), str(guest), vib_name, str(os.getpid()), str(os.getuid())],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            pidfd = -1
            try:
                with control.accept()[0] as peer:
                    peer.settimeout(8)
                    peer_is_process(peer, proc)
                    pidfd = os.pidfd_open(proc.pid, 0)
                    challenge = bytes(range(32))
                    peer.sendall(challenge)
                    assert read_exact(peer, len(challenge)) == challenge
                    peer.sendall(b"G")
                    assert read_exact(peer, 1) == b"I"  # Limited test rules, not an isolation attestation.
                with events.accept()[0] as event:
                    peer_is_process(event, proc)
                    event.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
                    event.sendall(b"RMEH" + struct.pack(">Q", 123))
                    for seq, expected in enumerate((501, 1, 1), 1):
                        assert select.select([event], [], [], 8)[0], "timed out waiting for vibration frame"
                        result = read_frame(event.fileno(), pidfd, proc.pid, os.getuid(), 123, seq)
                        assert result == expected, (seq, result, expected)
                    assert select.select([proc.stdout], [], [], 8)[0], "timed out waiting for Guest cleanup"
                    assert proc.stdout.read(1) == b"D"
                    proc.stdin.write(b"R")
                    proc.stdin.flush()
                _, err = proc.communicate(timeout=8)
                assert proc.returncode == 0, (proc.returncode, err)
            finally:
                if pidfd >= 0:
                    os.close(pidfd)
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=8)
    print("VIBRATION_FULL_CHAIN_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
