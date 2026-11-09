#!/usr/bin/env python3
"""Offline test against the real daemon with a self-created fake context manager."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / 'guest64' / '36'
with tempfile.TemporaryDirectory(prefix='rme-bproto-') as directory:
    path = Path(directory)
    daemon, client, sock = path / 'daemon', path / 'client', path / 'binder.sock'
    subprocess.run(['cc', '-std=c11', '-O1', str(SRC / 'binderd.c'), '-o', str(daemon)], check=True)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(SRC / 'vibration_bproto_client.c'), str(SRC / 'vibration_service_registration.c'),
                    str(SRC / 'tests/vibration_bproto_daemon_test.c'),
                    '-o', str(client)], check=True)
    proc = subprocess.Popen([str(daemon), str(sock)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 5
        while not sock.exists():
            if proc.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('daemon did not listen')
            time.sleep(0.01)
        subprocess.run([str(client), str(sock), str(proc.pid)], check=True, timeout=10)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
