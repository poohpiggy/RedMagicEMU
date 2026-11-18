# SPDX-License-Identifier: MIT
"""Run the isolated experiment in a bounded, private process group."""

import ctypes
import os
import signal
import subprocess
import sys
import time


class Interrupted(Exception):
    def __init__(self, signum):
        self.signum = signum


def parent_death_guard(parent):
    # If the supervisor is SIGKILLed, kill unshare. Its --kill-child and PID
    # namespace then tear down the experiment, rather than orphaning a server.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0:  # PR_SET_PDEATHSIG
        os._exit(125)
    if os.getppid() != parent:
        os._exit(125)


def terminate_group(process):
    # Keep the launcher unreaped until both signals are sent so its process
    # group ID cannot be reused by an unrelated process.
    for sig, delay in ((signal.SIGTERM, 0.25), (signal.SIGKILL, 0)):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        if delay:
            time.sleep(delay)
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        print("FAIL namespace supervisor did not exit after SIGKILL", file=sys.stderr)
        return None


def main():
    if len(sys.argv) < 4:
        return 2
    parent = os.getpid()
    command = ["unshare", "-Urnmpf", "--kill-child=KILL",
               "bash", sys.argv[1], "--inside", *sys.argv[2:]]

    def interrupted(signum, _frame):
        raise Interrupted(signum)

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, interrupted)
    process = None
    try:
        process = subprocess.Popen(command, start_new_session=True,
                                   preexec_fn=lambda: parent_death_guard(parent))
        status = process.wait(timeout=300)
        if status < 0:
            print(f"namespace_exit={status}", file=sys.stderr)
            return 128 - status
        return status
    except (Interrupted, subprocess.TimeoutExpired) as exc:
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            signal.signal(sig, signal.SIG_IGN)
        if process is not None:
            status = terminate_group(process)
            print(f"namespace_exit={status}", file=sys.stderr)
        if isinstance(exc, Interrupted):
            return 128 + exc.signum
        print("FAIL namespace experiment timed out", file=sys.stderr)
        return 124


if __name__ == "__main__":
    sys.exit(main())
