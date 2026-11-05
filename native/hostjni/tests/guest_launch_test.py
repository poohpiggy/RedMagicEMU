#!/usr/bin/env python3
"""Linux checks for the authenticated, pidfd-pinned exec launcher."""
import os
import pathlib
import secrets
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import ctypes

launcher = sys.argv[1]
probe = subprocess.run([launcher, "--probe-pidfd"], timeout=6)
assert probe.returncode == 0, f"isolated pidfd probe failed: {probe.returncode}"

# Android seccomp can kill the syscall caller instead of returning EPERM.  The
# disposable probe must fail without taking its parent down in that case.
class Filter(ctypes.Structure):
    _fields_ = [("code", ctypes.c_ushort), ("jt", ctypes.c_ubyte), ("jf", ctypes.c_ubyte), ("k", ctypes.c_uint)]
class Program(ctypes.Structure):
    _fields_ = [("len", ctypes.c_ushort), ("filter", ctypes.POINTER(Filter))]
def deny_pidfd_in_child():
    bpf = (Filter * 4)(Filter(0x20, 0, 0, 0), Filter(0x15, 0, 1, 434),
                       Filter(0x06, 0, 0, 0x80000000), Filter(0x06, 0, 0, 0x7fff0000))
    program = Program(4, bpf)
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(38, 1, 0, 0, 0) != 0 or libc.prctl(22, 2, ctypes.byref(program), 0, 0) != 0:
        os._exit(99)
blocked = subprocess.run([launcher, "--probe-pidfd"], preexec_fn=deny_pidfd_in_child, timeout=6)
assert blocked.returncode == -signal.SIGSYS, f"seccomp probe did not fail safely: {blocked.returncode}"

def server():
    name = "rme-test-" + secrets.token_hex(12)
    s = socket.socket(socket.AF_UNIX)
    s.bind("\0" + name)
    s.listen(1)
    s.settimeout(5)
    return name, s

def launch(command, stdin=subprocess.PIPE, **kwargs):
    name, s = server()
    p = subprocess.Popen([launcher, name, *command], stdin=stdin, stdout=subprocess.PIPE, **kwargs)
    conn, _ = s.accept()
    conn.settimeout(5)
    pid, uid, _ = struct.unpack("3i", conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    assert (pid, uid) == (p.pid, os.getuid())
    fd = os.pidfd_open(pid)
    signal.pidfd_send_signal(fd, 0)
    challenge = secrets.token_bytes(32)
    conn.sendall(challenge)
    response = b""
    while len(response) < len(challenge):
        chunk = conn.recv(len(challenge) - len(response))
        assert chunk
        response += chunk
    assert response == challenge
    conn.sendall(b"G")
    conn.close(); s.close()
    return p, fd

def finish(p, fd):
    os.close(fd)
    for stream in (p.stdin, p.stdout):
        if stream and not stream.closed:
            stream.close()

with tempfile.TemporaryDirectory() as d:
    p, fd = launch(["/bin/sh", "-c", 'printf "%s:%s:%s" "$$" "$1" "$2"; exit 37', "sh", "space arg", "ok"])
    assert p.stdout.read() == f"{p.pid}:space arg:ok".encode(), "launcher must exec, preserving pid and argv"
    assert p.wait(timeout=5) == 37
    finish(p, fd)

    p, fd = launch(["/bin/sh", "-c", 'trap "" TERM; echo ready; while :; do :; done'])
    assert p.stdout.readline() == b"ready\n"
    signal.pidfd_send_signal(fd, signal.SIGTERM)
    try:
        p.wait(timeout=.2)
        raise AssertionError("the test process must ignore SIGTERM")
    except subprocess.TimeoutExpired:
        pass
    signal.pidfd_send_signal(fd, signal.SIGKILL)
    assert p.wait(timeout=5) == -signal.SIGKILL
    finish(p, fd)

    p, fd = launch(["/bin/sh", "-c", "cat; echo done"])
    p.stdin.write(b"input preserved\n"); p.stdin.close()
    assert p.stdout.read() == b"input preserved\ndone\n"
    assert p.wait(timeout=5) == 0
    finish(p, fd)

    p, fd = launch(["/bin/sh", "-c", "read x || echo null"], stdin=subprocess.DEVNULL)
    assert p.stdout.read() == b"null\n" and p.wait(timeout=5) == 0
    finish(p, fd)

    left, right = socket.socketpair()
    right.settimeout(5)
    p, fd = launch([sys.executable, "-c", "import os,sys;os.write(int(sys.argv[1]),b'kept')", str(left.fileno())],
                   pass_fds=(left.fileno(),))
    assert right.recv(4) == b"kept" and p.wait(timeout=5) == 0
    finish(p, fd); left.close(); right.close()

    marker = pathlib.Path(d, "must-not-run")
    name, s = server()
    p = subprocess.Popen([launcher, name, "/bin/sh", "-c", f'touch "{marker}"'])
    conn, _ = s.accept(); conn.close(); s.close()
    assert p.wait(timeout=5) != 0 and not marker.exists(), "no challenge must prevent exec"

    name, s = server()
    p = subprocess.Popen([launcher, name, "/bin/sh", "-c", f'touch "{marker}"'])
    conn, _ = s.accept(); conn.sendall(bytes(32)); assert conn.recv(32) == bytes(32)
    conn.close(); s.close()
    assert p.wait(timeout=5) != 0 and not marker.exists(), "no commit must prevent exec"

    name, s = server()
    p = subprocess.Popen([launcher, name, "/bin/sh", "-c", f'touch "{marker}"'])
    conn, _ = s.accept()
    assert p.wait(timeout=7) != 0 and not marker.exists(), "missing challenge must time out"
    conn.close(); s.close()

    # Deliberately document scope: pidfd signals this process, not its forked child.
    p, fd = launch(["/bin/sh", "-c", 'sleep 30 & echo $!; wait'])
    child_fd = os.pidfd_open(int(p.stdout.readline()))
    try:
        signal.pidfd_send_signal(fd, signal.SIGKILL)
        assert p.wait(timeout=5) == -signal.SIGKILL
        signal.pidfd_send_signal(child_fd, 0)
    finally:
        signal.pidfd_send_signal(child_fd, signal.SIGKILL)
        os.close(child_fd); finish(p, fd)

print("authenticated pidfd launcher passed; forked descendants require separate cleanup")
