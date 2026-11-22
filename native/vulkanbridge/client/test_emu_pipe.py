#!/usr/bin/env python3
"""Exercise the client stream against a local Unix-socket peer, without a GPU."""
import array
import ctypes
import errno
import os
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    duplex_pipe = "--duplex-pipe" in sys.argv[1:]
    reply_pipe = "--reply-pipe" in sys.argv[1:] or duplex_pipe
    with tempfile.TemporaryDirectory(prefix="rme-vk-pipe-") as tmp:
        path = os.path.join(tmp, "socket")
        libpath = os.path.join(tmp, "pipe.so")
        counter_source = Path(tmp) / "send_counter.c"
        counter_source.write_text("""#include <sys/socket.h>
#include <unistd.h>
unsigned send_calls;
ssize_t __real_send(int fd, const void *data, size_t len, int flags);
ssize_t __wrap_send(int fd, const void *data, size_t len, int flags) {
    ++send_calls;
    return __real_send(fd, data, len, flags);
}
""")
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        *(["-DRME_VULKAN_REPLY_PIPE_TEST"] if reply_pipe else []),
                        *(["-DRME_VULKAN_DUPLEX_PIPE_TEST"] if duplex_pipe else []),
                        f'-DRME_VK_SOCKET_PATH="{path}"', str(HERE / "emu_pipe.c"),
                        str(counter_source), "-Wl,--wrap=send", "-o", libpath], check=True)
        lib = ctypes.CDLL(libpath, use_errno=True)
        lib.qemu_pipe_open.argtypes = [ctypes.c_char_p]
        lib.qemu_pipe_open.restype = ctypes.c_int
        lib.qemu_pipe_write_fully.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        lib.qemu_pipe_read_fully.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        lib.qemu_pipe_close.argtypes = [ctypes.c_int]
        lib.rme_vulkan_memory_fd.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_int)]
        lib.rme_vulkan_memory_fd.restype = ctypes.c_int

        assert lib.qemu_pipe_open(b"unknown") < 0
        assert lib.qemu_pipe_open(b"opengles") < 0  # server absent
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(path)
        listener.listen(2)
        listener.settimeout(3)
        received = []

        def decline(selector):
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(3)
                assert conn.recv(len(selector), socket.MSG_WAITALL) == selector

        def peer():
            duplex_selector = b"pipe:RmeVulkanDuplexPipe\0"
            for backpressure in (False, True) if duplex_pipe else ():
                conn, _ = listener.accept()
                reply_reader, reply_writer = os.pipe()
                command_reader, command_writer = os.pipe2(os.O_NONBLOCK)
                try:
                    with conn:
                        conn.settimeout(3)
                        assert conn.recv(len(duplex_selector), socket.MSG_WAITALL) == duplex_selector
                        conn.sendmsg([b"D"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                                               array.array("i", [reply_reader, command_writer, command_reader]))])
                        if backpressure:
                            # Disconnect with pending commands and no reader progress.
                            assert select.select([command_reader], [], [], 3)[0]
                        else:
                            os.set_blocking(command_reader, True)
                            assert os.read(command_reader, 3) == b"abc"
                            os.write(reply_writer, b"xyz")
                finally:
                    for fd in (reply_reader, reply_writer, command_reader, command_writer):
                        os.close(fd)
            if duplex_pipe:
                decline(duplex_selector)
            if reply_pipe:
                selector = b"pipe:RmeVulkanReplyPipe\0"
                conn, _ = listener.accept()
                reader, writer = os.pipe()
                try:
                    with conn:
                        conn.settimeout(3)
                        assert conn.recv(len(selector), socket.MSG_WAITALL) == selector
                        conn.sendmsg([b"P"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [reader]))])
                        assert conn.recv(3, socket.MSG_WAITALL) == b"abc"
                        os.write(writer, b"xyz")
                finally:
                    os.close(reader)
                    os.close(writer)
                if duplex_pipe:
                    decline(duplex_selector)
                decline(selector)
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
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(2)
                request = b"pipe:RmeVulkanMemory2\0" + struct.pack("=Q", 0x0102030405060708)
                assert conn.recv(len(request), socket.MSG_WAITALL) == request
                with tempfile.TemporaryFile() as memory:
                    memory.write(b"fd contents")
                    memory.seek(0)
                    conn.sendmsg([b"H"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                                           array.array("i", [memory.fileno()]))])
            # An older renderer closes the new selector and accepts the legacy one.
            conn, _ = listener.accept()
            with conn:
                assert conn.recv(len(request), socket.MSG_WAITALL) == request
            conn, _ = listener.accept()
            with conn, tempfile.TemporaryFile() as memory:
                request = b"pipe:RmeVulkanMemory\0" + struct.pack("=Q", 0x0102030405060708)
                assert conn.recv(len(request), socket.MSG_WAITALL) == request
                conn.sendmsg([b"M"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                                       array.array("i", [memory.fileno()]))])

        worker = threading.Thread(target=peer)
        worker.start()
        if duplex_pipe:
            fd = lib.qemu_pipe_open(b"opengles")
            assert fd >= 0
            assert lib.qemu_pipe_write_fully(fd, b"abc", 3) == 0
            out = ctypes.create_string_buffer(3)
            assert lib.qemu_pipe_read_fully(fd, out, 3) == 0 and out.raw == b"xyz"
            assert lib.qemu_pipe_read_fully(fd, out, 1) == -errno.EPIPE
            assert lib.qemu_pipe_write_fully(fd, b"x", 1) == -errno.EPIPE
            lib.qemu_pipe_close(fd)
            fd = lib.qemu_pipe_open(b"opengles")
            assert fd >= 0
            pending = bytes(2 * 1024 * 1024)
            assert lib.qemu_pipe_write_fully(fd, pending, len(pending)) == -errno.EPIPE
            lib.qemu_pipe_close(fd)
        names = (b"opengles", b"GLProcessPipe")
        if reply_pipe:
            names = (b"opengles",) + names
        for name in names:
            fd = lib.qemu_pipe_open(name)
            assert fd >= 0
            assert lib.qemu_pipe_write_fully(fd, b"abc", 3) == 0
            out = ctypes.create_string_buffer(3)
            assert lib.qemu_pipe_read_fully(fd, out, 3) == 0
            assert out.raw == b"xyz"
            assert lib.qemu_pipe_read_fully(fd, out, 1) < 0  # EOF must not spin
            assert lib.qemu_pipe_write_fully(fd, b"x", 1) < 0  # no SIGPIPE
            lib.qemu_pipe_close(fd)
        send_calls = ctypes.c_uint.in_dll(lib, "send_calls")
        send_calls.value = 0
        mode = ctypes.c_int(-1)
        fd = lib.rme_vulkan_memory_fd(0x0102030405060708, ctypes.byref(mode))
        assert fd >= 0
        with os.fdopen(fd, "rb") as memory:
            assert memory.read() == b"fd contents" and mode.value == 2
        assert send_calls.value == 1, f"shared memory request used {send_calls.value} sends"
        fd = lib.rme_vulkan_memory_fd(0x0102030405060708, ctypes.byref(mode))
        assert fd >= 0 and mode.value == 0
        os.close(fd)
        worker.join(timeout=5)
        assert not worker.is_alive() and len(received) == 2
        listener.close()
    print("EMU pipe protocol and EOF: OK")


if __name__ == "__main__":
    main()
