#!/usr/bin/env python3
"""Exercise the client stream against a local Unix-socket peer, without a GPU."""
import ctypes
import os
import socket
import subprocess
import tempfile
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix="rme-vk-pipe-") as tmp:
        path = os.path.join(tmp, "socket")
        libpath = os.path.join(tmp, "pipe.so")
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        f'-DRME_VK_SOCKET_PATH="{path}"', str(HERE / "emu_pipe.c"),
                        "-o", libpath], check=True)
        lib = ctypes.CDLL(libpath, use_errno=True)
        lib.qemu_pipe_open.argtypes = [ctypes.c_char_p]
        lib.qemu_pipe_open.restype = ctypes.c_int
        lib.qemu_pipe_write_fully.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        lib.qemu_pipe_read_fully.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        lib.qemu_pipe_close.argtypes = [ctypes.c_int]

        assert lib.qemu_pipe_open(b"unknown") < 0
        assert lib.qemu_pipe_open(b"opengles") < 0  # server absent
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(path)
        listener.listen(2)
        received = []

        def peer():
            for expected in (b"pipe:opengles\0", b"pipe:GLProcessPipe\0"):
                conn, _ = listener.accept()
                with conn:
                    prefix = b""
                    while not prefix.endswith(b"\0"):
                        chunk = conn.recv(1)
                        assert chunk
                        prefix += chunk
                    received.append(prefix)
                    assert prefix == expected
                    data = b""
                    while len(data) < 3:
                        chunk = conn.recv(3 - len(data))
                        assert chunk
                        data += chunk
                    assert data == b"abc"
                    conn.sendall(b"x")
                    conn.sendall(b"yz")

        worker = threading.Thread(target=peer)
        worker.start()
        for name in (b"opengles", b"GLProcessPipe"):
            fd = lib.qemu_pipe_open(name)
            assert fd >= 0
            assert lib.qemu_pipe_write_fully(fd, b"abc", 3) == 0
            out = ctypes.create_string_buffer(3)
            assert lib.qemu_pipe_read_fully(fd, out, 3) == 0
            assert out.raw == b"xyz"
            assert lib.qemu_pipe_read_fully(fd, out, 1) < 0  # EOF must not spin
            assert lib.qemu_pipe_write_fully(fd, b"x", 1) < 0  # no SIGPIPE
            lib.qemu_pipe_close(fd)
        worker.join(timeout=5)
        assert not worker.is_alive() and len(received) == 2
        listener.close()
    print("EMU pipe protocol and EOF: OK")


if __name__ == "__main__":
    main()
