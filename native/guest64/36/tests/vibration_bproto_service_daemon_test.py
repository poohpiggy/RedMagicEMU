#!/usr/bin/env python3
"""Exercise an offline fake manager and client through the actual local binderd."""
from pathlib import Path
import subprocess
import tempfile
import time

SRC = Path(__file__).resolve().parents[1]
SOURCES = ['vibration_bproto_client.c', 'vibration_bproto_lifecycle.c',
           'vibration_service_registration.c', 'vibration_binder_parcel.c',
           'vibration_binder_contract.c', 'vibration_binder_reply.c',
           'tests/vibration_bproto_service_fixture.c', 'tests/vibration_bproto_service_daemon_test.c']
with tempfile.TemporaryDirectory(prefix='rme-vib-service-') as directory:
    work = Path(directory)
    daemon, test, sock = work / 'daemon', work / 'test', work / 'binder.sock'
    subprocess.run(['cc', '-std=c11', '-O1', str(SRC / 'binderd.c'), '-o', str(daemon)], check=True)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    *(str(SRC / p) for p in SOURCES), '-o', str(test)], check=True)
    proc = subprocess.Popen([str(daemon), str(sock)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 5
        while not sock.exists():
            if proc.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('daemon did not listen')
            time.sleep(0.01)
        subprocess.run([str(test), str(sock), str(proc.pid)], check=True, timeout=15)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
