"""Real-process test of the experimental vibration launcher (Linux only).

Uses self-created files and executables. This is not an Android or hardware
test and does not establish full isolation of a real Guest service.
"""
from pathlib import Path
import ctypes
import os
import socket
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
ENGINE = r'''
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "normal")) {
        if (chmod(argv[2], 0600) != 0) return 9;
        puts("NORMAL_OK"); return 0;
    }
    if (argc != 6 || strcmp(argv[1], "guarded")) return 2;
    int inherited = atoi(argv[4]);
    if (fcntl(inherited, F_GETFD) != -1 || errno != EBADF) return 3;
    int inside = open(argv[2], O_RDONLY);
    if (inside < 0) return 4;
    close(inside);
    errno = 0;
    int outside = open(argv[3], O_RDONLY);
    if (outside != -1 || errno != EACCES) return 5;
    int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0 || write(nullfd, "x", 1) != 1) return 10;
    close(nullfd);
    int randomfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    char sample;
    if (randomfd < 0 || read(randomfd, &sample, 1) != 1) return 11;
    close(randomfd);
    errno = 0;
    if (open("/dev/urandom", O_WRONLY | O_CLOEXEC) != -1 || errno != EACCES) return 17;
    int statusfd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (statusfd < 0 || read(statusfd, &sample, 1) != 1) return 12;
    close(statusfd);
    int mapsfd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (mapsfd < 0 || read(mapsfd, &sample, 1) != 1) return 13;
    close(mapsfd);
    int self_fds = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (self_fds < 0) return 14;
    close(self_fds);
    errno = 0;
    if (open(argv[5], O_RDONLY | O_CLOEXEC) != -1 || errno != EACCES) return 15;
    errno = 0;
    if (open("/dev/zero", O_RDONLY | O_CLOEXEC) != -1 || errno != EACCES) return 16;
    errno = 0;
    if (chmod(argv[2], 0644) != -1 || errno != EPERM) return 6;
    errno = 0;
    if (setxattr(argv[2], "user.rme", "x", 1, 0) != -1 || errno != EPERM) return 7;
    errno = 0;
    if (utimensat(AT_FDCWD, argv[2], NULL, 0) != -1 || errno != EPERM) return 8;
    errno = 0;
    if (syscall(463, AT_FDCWD, NULL, 0, NULL, 0) != -1 || errno != EPERM) return 10;
    errno = 0;
    if (syscall(466, AT_FDCWD, NULL, 0, NULL) != -1 || errno != EPERM) return 11;
    puts("GUARDED_OK");
    return 0;
}
'''


def run_case(launcher, engine, guest, good, bad, guarded, n):
    name = f"rme-vib-launch-{os.getpid()}-{n}"
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.settimeout(8)
    server.bind("\0" + name)
    server.listen(1)
    inherited = os.open(bad, os.O_RDONLY)
    if guarded:
        command = [str(launcher), name, "--experimental-vibration-guard", str(guest),
                   str(engine), "guarded", str(good), str(bad), str(inherited),
                   f"/proc/{os.getpid()}/status"]
    else:
        command = [str(launcher), name, str(engine), "normal", str(good)]
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, pass_fds=(inherited,))
    try:
        conn, _ = server.accept()
        conn.settimeout(8)
        with conn:
            pid, uid, _ = struct.unpack("3i", conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            assert pid == proc.pid and uid == os.getuid()
            challenge = bytes(range(32))
            conn.sendall(challenge)
            answer = b""
            while len(answer) < len(challenge):
                part = conn.recv(len(challenge) - len(answer))
                if not part: break
                answer += part
            assert answer == challenge
            conn.sendall(b"G")
            if guarded:
                receipt = conn.recv(1)
                assert receipt == b"I", f"expected experimental guard receipt, got {receipt!r}"
        out, err = proc.communicate(timeout=8)
        assert proc.returncode == 0, (proc.returncode, out, err)
        assert (b"GUARDED_OK" if guarded else b"NORMAL_OK") in out, (out, err)
    finally:
        os.close(inherited)
        server.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)


def run_rejected(launcher, engine, guest, n, invalid_stdio=False):
    name = f"rme-vib-launch-reject-{os.getpid()}-{n}"
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.settimeout(8)
    server.bind("\0" + name)
    server.listen(1)
    command = [str(launcher), name, "--experimental-vibration-guard", str(guest), str(engine), "normal"]
    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL if invalid_stdio else subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        conn, _ = server.accept()
        conn.settimeout(8)
        with conn:
            challenge = bytes(range(32))
            conn.sendall(challenge)
            answer = b""
            while len(answer) < len(challenge):
                part = conn.recv(len(challenge) - len(answer))
                if not part: break
                answer += part
            assert answer == challenge
            conn.sendall(b"G")
            assert conn.recv(1) == b"", "failed guard must never issue a receipt"
        out, err = proc.communicate(timeout=8)
        assert proc.returncode == 125 and b"NORMAL_OK" not in out, (proc.returncode, out, err)
    finally:
        server.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)


def main():
    libc = ctypes.CDLL(None, use_errno=True)
    abi = libc.syscall(444, 0, 0, 1)
    if abi < 5:
        print("VIBRATION_LAUNCHER_SKIPPED_LANDLOCK")
        return 77
    with tempfile.TemporaryDirectory(prefix="rme-vibration-launch-") as tmp:
        tmp = Path(tmp)
        guest = tmp / "guest"
        guest.mkdir()
        good = guest / "good"
        bad = tmp / "outside"
        good.write_text("guest")
        bad.write_text("outside")
        source = tmp / "engine.c"
        source.write_text(ENGINE)
        engine = tmp / "engine"
        launcher = tmp / "launcher"
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-static",
                        str(source), "-o", str(engine)], check=True)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "native/hostjni/guest_launch.c"),
                        str(ROOT / "native/hostjni/vibration_metadata_guard.c"), "-o", str(launcher)], check=True)
        run_case(launcher, engine, guest, good, bad, False, 1)
        run_case(launcher, engine, guest, good, bad, True, 2)
        run_rejected(launcher, engine, tmp / "missing", 1)
        engine_link = tmp / "engine-link"
        engine_link.symlink_to(engine)
        run_rejected(launcher, engine_link, guest, 2)
        run_rejected(launcher, engine, guest, 3, invalid_stdio=True)
    print("VIBRATION_LAUNCHER_TEST_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
