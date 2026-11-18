# SPDX-License-Identifier: MIT
"""Exercise cleanup using only fixture servers created by this test."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def wait_marker(path, process):
    for _ in range(150):
        if path.exists():
            return int(path.read_text())
        if process.poll() is not None:
            raise RuntimeError(f"fixture exited early: {process.returncode}")
        time.sleep(0.1)
    raise RuntimeError("fixture did not start")


def assert_no_fixture(pid, fixture):
    for _ in range(50):
        try:
            cmdline = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
            status = Path(f"/proc/{pid}/status").read_text()
        except FileNotFoundError:
            return
        if os.fsencode(fixture) not in cmdline or "\nState:\tZ" in status:
            return
        time.sleep(0.1)
    raise RuntimeError(f"fixture server {pid} remains alive")


def run():
    if len(sys.argv) != 8:
        print("usage: test-lifecycle.py BASELINE_ICD PATCHED_ICD SERVER BACKEND_DIR DEP_LIB_DIR NDK VULKAN_INCLUDE", file=sys.stderr)
        return 2
    here = Path(__file__).resolve().parent
    out = Path(tempfile.mkdtemp(prefix="rme-memory-lifecycle-", dir="/var/tmp"))
    marker = out / "ready"
    fixture = out / "fixture.py"
    fixture.write_text("""#!/usr/bin/python3
import os, pathlib, signal, socket
signal.signal(signal.SIGTERM, signal.SIG_IGN)
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.bind("/tmp/kumquat-gpu-0")
s.listen(8)
for line in pathlib.Path("/proc/self/status").read_text().splitlines():
    if line.startswith("NSpid:"):
        pathlib.Path(os.environ["RME_LIFECYCLE_MARKER"]).write_text(line.split()[1])
        break
while True:
    client, _ = s.accept()
    if os.environ["RME_LIFECYCLE_MODE"] in ("close", "stop"):
        client.close()
        if os.environ["RME_LIFECYCLE_MODE"] == "stop":
            os.kill(os.getpid(), signal.SIGSTOP)
    else:
        signal.pause()
""")
    fixture.chmod(0o700)
    args = list(sys.argv[1:])
    args[2] = str(fixture)
    command = ["bash", str(here / "verify.sh"), *args]
    env = os.environ.copy()
    env["RME_LIFECYCLE_MARKER"] = str(marker)
    print(f"lifecycle_logs={out}", flush=True)

    for name, mode, sig in (("ignored_term", "close", None),
                            ("stopped_server", "stop", None),
                            ("supervisor_term", "hang", signal.SIGTERM),
                            ("supervisor_kill", "hang", signal.SIGKILL)):
        marker.unlink(missing_ok=True)
        env["RME_LIFECYCLE_MODE"] = mode
        with (out / f"{name}.log").open("wb") as log:
            process = subprocess.Popen(command, env=env, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                pid = wait_marker(marker, process)
                if sig is not None:
                    os.kill(process.pid, sig)
                status = process.wait(timeout=15)
            except BaseException:
                # The test owns this isolated group; never signal other jobs.
                if process.poll() is None:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                process.wait(timeout=5)
                raise
        assert_no_fixture(pid, fixture)
        output = (out / f"{name}.log").read_text(errors="replace")
        if sig is None:
            if status != 10 or "server_exit=137 term_sent=1 forced_kill=1" not in output:
                raise RuntimeError(f"{name}: status={status}; see {out}")
        elif status != (143 if sig == signal.SIGTERM else -signal.SIGKILL):
            raise RuntimeError(f"{name}: unexpected status={status}")
        print(f"{name}: status={status} fixture_alive=false", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(run())
