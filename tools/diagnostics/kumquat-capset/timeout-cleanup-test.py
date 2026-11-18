#!/usr/bin/env python3
"""Verify that timeout cleanup kills only the test's own process group."""
import argparse
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent


def alive(pid):
    try:
        state = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
        return state != "Z"
    except (FileNotFoundError, ProcessLookupError):
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", action="store_true",
                        help="reproduce the old subprocess.run timeout behavior")
    args = parser.parse_args()
    directory = Path(tempfile.mkdtemp(prefix="rme-eof-timeout-", dir="/var/tmp"))
    parent_file = directory / "parent.pid"
    child_file = directory / "child.pid"
    descendant = (
        "import os,signal,time; from pathlib import Path; "
        "signal.signal(signal.SIGTERM,signal.SIG_IGN); "
        f"Path({str(child_file)!r}).write_text(str(os.getpid())); "
        "time.sleep(60)"
    )
    parent = (
        "import os,subprocess,sys,time; from pathlib import Path; "
        f"Path({str(parent_file)!r}).write_text(str(os.getpid())); "
        f"subprocess.Popen([sys.executable,'-c',{descendant!r}]); "
        "time.sleep(60)"
    )
    cmd = [sys.executable, "-c", parent]
    sentinel = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                                start_new_session=True)
    owner_pid = child_pid = None
    try:
        if args.baseline:
            try:
                subprocess.run(cmd, start_new_session=True, timeout=1.5)
                print("FAIL baseline unexpectedly completed")
                return 1
            except subprocess.TimeoutExpired:
                timed_out = True
        else:
            spec = importlib.util.spec_from_file_location("rme_eof_run", HERE / "run.py")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            if not hasattr(module, "run_in_new_group"):
                print("FAIL bounded process-group cleanup is missing")
                return 1
            _, timed_out = module.run_in_new_group(cmd, timeout=1.5)
        if not parent_file.exists() or not child_file.exists():
            print("FAIL test processes did not start")
            return 1
        owner_pid = int(parent_file.read_text())
        child_pid = int(child_file.read_text())
        deadline = time.monotonic() + 2
        while alive(child_pid) and time.monotonic() < deadline:
            time.sleep(0.05)
        child_alive = alive(child_pid)
        sentinel_alive = sentinel.poll() is None
        print(f"timed_out={timed_out} parent_pid={owner_pid} descendant_pid={child_pid} "
              f"descendant_alive={child_alive} unrelated_sentinel_alive={sentinel_alive}")
        if not timed_out or child_alive or not sentinel_alive:
            print("FAIL timeout cleanup")
            return 1
        print("PASS timeout cleanup")
        return 0
    finally:
        # Only kill the process group created by this test, after verifying its ID.
        if child_pid is None and child_file.exists():
            child_pid = int(child_file.read_text())
        if owner_pid is None and parent_file.exists():
            owner_pid = int(parent_file.read_text())
        if child_pid and owner_pid and alive(child_pid):
            try:
                if os.getpgid(child_pid) == owner_pid and owner_pid != os.getpgrp():
                    os.killpg(owner_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        if sentinel.poll() is None:
            sentinel.terminate()
        try:
            sentinel.wait(timeout=3)
        except subprocess.TimeoutExpired:
            sentinel.kill()
            sentinel.wait()


if __name__ == "__main__":
    sys.exit(main())
