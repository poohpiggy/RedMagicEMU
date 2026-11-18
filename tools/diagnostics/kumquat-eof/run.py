#!/usr/bin/env python3
"""Exercise Kumquat EOF handling on real private Unix sockets."""
import argparse
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
QUERY = struct.pack("<II", 0x101, 0)
REPLY = struct.pack("<II", 0x3002, 1)


def connect():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(3)
    s.connect("/tmp/kumquat-gpu-0")
    return s


def wait_for_fd_count(pid, expected):
    deadline = time.monotonic() + 2
    count = -1
    while time.monotonic() < deadline:
        count = len(os.listdir(f"/proc/{pid}/fd"))
        if count == expected:
            return count
        time.sleep(0.05)
    return count


def child(args):
    parent_mnt = os.environ.pop("RME_EOF_PARENT_MNT", "")
    if not parent_mnt or os.readlink("/proc/self/ns/mnt") == parent_mnt:
        raise RuntimeError("refusing to bind /tmp outside a new mount namespace")
    out = Path(args.output)
    private_tmp = out / "private-tmp"
    private_tmp.mkdir(mode=0o700)
    subprocess.run(["mount", "--bind", str(private_tmp), "/tmp"], check=True)
    gate = out / "submit-gate.so"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                    "-I" + args.vulkan_include, str(HERE / "submit-gate.c"), "-ldl",
                    "-o", str(gate)], check=True)

    common = os.environ.copy()
    common.pop("VK_DRIVER_FILES", None)
    common["LD_LIBRARY_PATH"] = args.library_path
    server_env = common.copy()
    server_env["VK_ICD_FILENAMES"] = args.host_icd
    probe_env = common.copy()
    probe_env["VK_ICD_FILENAMES"] = args.client_icd
    probe_env["VIRTGPU_KUMQUAT"] = "1"
    ready_r, ready_w = os.pipe()
    go_r, go_w = os.pipe()
    server = client = None
    failed = False
    with (out / "server.log").open("wb") as server_log, (out / "existing-client.log").open("wb") as client_log:
        try:
            server = subprocess.Popen(
                [args.server, "--gpu-socket-path=/tmp/kumquat-gpu-0",
                 "--capset-names=gfxstream-vulkan",
                 "--renderer-features=VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled"],
                stdout=server_log, stderr=subprocess.STDOUT, env=server_env)
            deadline = time.monotonic() + 20
            while not Path("/tmp/kumquat-gpu-0").is_socket():
                if server.poll() is not None:
                    raise RuntimeError(f"server exited early: {server.returncode}")
                if time.monotonic() >= deadline:
                    raise RuntimeError("server socket did not appear")
                time.sleep(0.05)
            probe_env["LD_PRELOAD"] = str(gate)
            probe_env["RME_TEST_READY_FD"] = str(ready_w)
            probe_env["RME_TEST_RESUME_FD"] = str(go_r)
            client = subprocess.Popen([args.probe], stdout=client_log, stderr=subprocess.STDOUT,
                                      env=probe_env, pass_fds=(ready_w, go_r))
            if os.getpgid(server.pid) != os.getpgrp() or os.getpgid(client.pid) != os.getpgrp():
                raise RuntimeError("server or client escaped the test process group")
            os.close(ready_w)
            ready_w = -1
            os.close(go_r)
            go_r = -1
            if not select.select([ready_r], [], [], 20)[0] or os.read(ready_r, 1) != b"R":
                raise RuntimeError("existing Vulkan client did not reach submit gate")
            time.sleep(0.2)
            initial = len(os.listdir(f"/proc/{server.pid}/fd"))
            for _ in range(10):
                s = connect()
                s.close()
            empty = wait_for_fd_count(server.pid, initial)
            for _ in range(10):
                with connect() as s:
                    s.sendall(QUERY)
                    if s.recv(8) != REPLY:
                        raise RuntimeError("single capset-count query failed")
            queried = wait_for_fd_count(server.pid, initial)
            queued_ok = 0
            for _ in range(10):
                with connect() as s:
                    s.sendall(QUERY)
                    s.sendall(QUERY)
                    s.shutdown(socket.SHUT_WR)
                    if s.recv(8) == REPLY and s.recv(8) == REPLY:
                        queued_ok += 1
            queued = wait_for_fd_count(server.pid, initial)
            print(f"server_pid={server.pid} existing_client_pid={client.pid}", flush=True)
            print(f"initial_fds={initial} after_empty_eof={empty} "
                  f"after_query_eof={queried} after_queued_eof={queued}", flush=True)
            print(f"queued_two_messages_success={queued_ok}/10", flush=True)
            if (empty, queried, queued) != (initial, initial, initial) or queued_ok != 10:
                failed = True
            os.write(go_w, b"G")
            existing_status = client.wait(timeout=25)
            client_log.flush()
            existing_output = (out / "existing-client.log").read_text(errors="replace")
            print(f"existing_compute_status={existing_status}", flush=True)
            if existing_status != 0 or "VULKAN_COMPUTE_OK words=64" not in existing_output:
                failed = True
            probe_env.pop("LD_PRELOAD", None)
            probe_env.pop("RME_TEST_READY_FD", None)
            probe_env.pop("RME_TEST_RESUME_FD", None)
            with (out / "new-client.log").open("wb") as new_log:
                new = subprocess.run([args.probe], stdout=new_log, stderr=subprocess.STDOUT,
                                     env=probe_env, timeout=25)
            new_output = (out / "new-client.log").read_text(errors="replace")
            print(f"new_compute_status={new.returncode}", flush=True)
            if new.returncode != 0 or "VULKAN_COMPUTE_OK words=64" not in new_output:
                failed = True
            alive = server.poll() is None
            print(f"server_alive_after_test={alive}", flush=True)
            if not alive:
                failed = True
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL test error: {exc}", flush=True)
            failed = True
        finally:
            if client and client.poll() is None:
                client.terminate()
                try:
                    client.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    client.kill()
                    client.wait()
            for fd in (ready_r, ready_w, go_r, go_w):
                if fd >= 0:
                    os.close(fd)
            if server and server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
            if server:
                print(f"server_exit_after_test={server.returncode}", flush=True)
                if server.returncode != -15:
                    failed = True
    print("FAIL EOF lifecycle" if failed else "PASS EOF lifecycle", flush=True)
    return 1 if failed else 0


def run_in_new_group(command, timeout, env=None):
    # The namespace launcher, server, and clients inherit this new process group.
    # Keep the launcher unreaped until both signals are sent so its PID cannot be
    # reused as an unrelated process-group ID during cleanup.
    process = subprocess.Popen(command, env=env, start_new_session=True)
    try:
        return process.wait(timeout=timeout), False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        time.sleep(0.25)
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)
        return process.returncode, True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--client-icd", required=True)
    parser.add_argument("--host-icd", required=True)
    parser.add_argument("--vulkan-include", required=True)
    parser.add_argument("--library-path", required=True)
    parser.add_argument("--_child", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--output", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args._child:
        if not args.output:
            parser.error("internal child requires output directory")
        return child(args)
    out = tempfile.mkdtemp(prefix="rme-kumquat-eof-", dir="/var/tmp")
    print(f"result_dir={out}", flush=True)
    cmd = ["unshare", "--user", "--map-root-user", "--mount", "--propagation", "private",
           sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], "--_child", "--output", out]
    child_env = os.environ.copy()
    child_env["RME_EOF_PARENT_MNT"] = os.readlink("/proc/self/ns/mnt")
    status, timed_out = run_in_new_group(cmd, timeout=100, env=child_env)
    if timed_out:
        print("FAIL namespace test timed out; test process group terminated", flush=True)
        return 1
    return status


if __name__ == "__main__":
    sys.exit(main())
