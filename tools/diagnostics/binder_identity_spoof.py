#!/usr/bin/env python3
"""Reproduce a Binder identity defect locally; this is not a security pass.

Run on a 64-bit little-endian Linux machine with Python 3 and gcc:
    python3 tools/diagnostics/binder_identity_spoof.py

The script compiles the unmodified API-36 binderd in a private temporary
directory. Both clients and the daemon are synthetic local processes; no guest,
phone, privileged device or product service is contacted. A successful
reproduction means that forged caller fields reached the synthetic receiver.
It does not establish that a regular APK can access the daemon's socket.
"""

import os
import pathlib
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "native/guest64/36/binderd.c"
HEADER = struct.Struct("<4I")
HELLO = struct.Struct("<8I")
TRANSACTION = struct.Struct("<8I3Q")
DELIVERY = struct.Struct("<6IiI6Q")
CREDS = struct.Struct("3i")
OP_HELLO, OP_BCS, OP_TXN, OP_READ = 1, 2, 3, 4
F_READ, F_NONBLOCK, F_TXN, F_AGAIN = 1, 2, 8, 16
BC_AEMU_CTX_MGR, BC_TRANSACTION, BR_TRANSACTION = 0x633e, 0x40406300, 0x80407202


def connect(path):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    sock.settimeout(5)
    sock.connect(path)
    return sock


def hello(sock, pid, uid):
    sock.sendall(HELLO.pack(OP_HELLO, 0, 0, uid, pid, pid, 1, 1))


def synthetic_sender(path, notify, forged_pid, forged_uid):
    try:
        with connect(path) as sock:
            # Kernel-generated credentials on a separate private socket let the
            # parent independently establish the sender's real process identity.
            notify.send(b"R")
            if notify.recv(1) != b"G":
                os._exit(2)
            hello(sock, forged_pid, forged_uid)
            sock.sendall(TRANSACTION.pack(OP_TXN, 0, 0, forged_uid,
                                         BC_TRANSACTION, 0, 42, 0, 0, 0, 0))
            notify.send(b"S")
            if notify.recv(1) != b"D":
                os._exit(3)
        os._exit(0)
    except BaseException:
        os._exit(4)


def run():
    if sys.platform != "linux" or struct.calcsize("P") != 8 or sys.byteorder != "little":
        raise RuntimeError("This diagnostic requires 64-bit little-endian Linux")
    with tempfile.TemporaryDirectory(prefix="rme-binder-diag-") as temp:
        binary = pathlib.Path(temp) / "binderd"
        path = str(pathlib.Path(temp) / "binder.sock")
        subprocess.run(["gcc", "-std=gnu11", "-O2", str(SOURCE), "-o", str(binary)],
                       check=True, timeout=60)
        daemon = subprocess.Popen([str(binary), path], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, start_new_session=True)
        child = None
        try:
            deadline = time.monotonic() + 5
            while not os.path.exists(path):
                if daemon.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("synthetic daemon did not start")
                time.sleep(.01)
            with connect(path) as manager, socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET) as_unused:
                pass
        finally:
            if child is not None:
                try:
                    os.kill(child, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                os.waitpid(child, 0)
            if daemon.poll() is None:
                os.killpg(daemon.pid, signal.SIGTERM)
            try:
                daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(daemon.pid, signal.SIGKILL)
                daemon.wait(timeout=5)


if __name__ == "__main__":
    try:
        run()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as exc:
        print(f"Diagnostic inconclusive: {exc}", file=sys.stderr)
        sys.exit(1)
