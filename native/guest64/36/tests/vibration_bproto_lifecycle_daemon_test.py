#!/usr/bin/env python3
"""Exercise lifecycle messages against the actual daemon, using fake peers only."""
from pathlib import Path
import subprocess
import tempfile
import time

src = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="rme-vib-lifecycle-") as directory:
    path = Path(directory)
    daemon, client, sock = path / "daemon", path / "client", path / "binder.sock"
    subprocess.run(["cc", "-std=c11", "-O1", str(src / "binderd.c"), "-o", str(daemon)], check=True)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(src / "vibration_bproto_client.c"), str(src / "vibration_bproto_lifecycle.c"),
                    str(src / "tests/vibration_bproto_lifecycle_daemon_test.c"), "-o", str(client)], check=True)
    proc = subprocess.Popen([str(daemon), str(sock)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 5
        while not sock.exists():
            if proc.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("daemon did not listen")
            time.sleep(0.01)
        subprocess.run([str(client), str(sock), str(proc.pid)], check=True, timeout=10)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
