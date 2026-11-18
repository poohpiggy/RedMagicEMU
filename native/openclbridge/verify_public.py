"""Run public OpenCL consumer programs with their own host sessions."""

import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading


out = Path(sys.argv[1])
driver = os.environ.get('RME_CL_HOST_ICD', 'libOpenCL.so.1')
windows_worker = os.environ.get('RME_CL_WINDOWS_WORKER')
env = os.environ.copy()
env['LD_LIBRARY_PATH'] = str(out / 'guest')


def pump_windows(client, worker, counts):
    def send_requests():
        header = struct.Struct('<IIII11QII')
        pending = bytearray()
        with worker.stdin:
            worker.stdin.write(struct.pack('<Q', (1 << 64) - 1))
            while data := client.recv(65536):
                if counts is not None:
                    pending.extend(data)
                    while len(pending) >= header.size:
                        fields = header.unpack_from(pending)
                        size = header.size + fields[2]
                        if len(pending) < size:
                            break
                        counts['frames'] += 1
                        counts['separate'] += fields[1] == (1 << 30) | 18
                        counts['inline'] += fields[-1] != 0
                        del pending[:size]
                worker.stdin.write(data)
                worker.stdin.flush()

    sender = threading.Thread(target=send_requests, daemon=True)
    sender.start()
    try:
        while data := os.read(worker.stdout.fileno(), 65536):
            client.sendall(data)
    finally:
        client.shutdown(socket.SHUT_RDWR)
        sender.join(timeout=5)


def run_windows(arguments, executable, counts=None):
    path = out / 'aemucl'
    try:
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(str(path))
            listener.listen(1)
            listener.settimeout(10)
            with subprocess.Popen([out / executable, *arguments], env=env, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as probe:
                try:
                    client, _ = listener.accept()
                    with client, subprocess.Popen([windows_worker], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as worker:
                        try:
                            pump_windows(client, worker, counts)
                            if worker.wait(timeout=5):
                                raise RuntimeError(f'Windows worker exited {worker.returncode}')
                        finally:
                            if worker.poll() is None:
                                worker.kill()
                    result, _ = probe.communicate(timeout=10)
                    return probe.returncode, result
                finally:
                    if probe.poll() is None:
                        probe.kill()
    finally:
        path.unlink(missing_ok=True)


def verify(arguments, status, marker, executable='probe', host_driver=driver):
    if windows_worker:
        actual, output = run_windows(arguments, executable)
        print(output, end='', flush=True)
        if actual != status or marker not in output:
            raise RuntimeError(f'{executable} {arguments} exited {actual}, expected {status} with {marker}')
        return
    socket = out / 'aemucl'
    with subprocess.Popen([out / 'host', host_driver, str(socket)], stdout=subprocess.PIPE, text=True) as host:
        try:
            if host.stdout.readline().strip() != 'READY':
                raise RuntimeError('Host failed to open the local stream')
            probe = subprocess.run([out / executable, *arguments], env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
            print(probe.stdout, end='', flush=True)
            if probe.returncode != status or marker not in probe.stdout:
                raise RuntimeError(f'{executable} {arguments} exited {probe.returncode}, expected {status} with {marker}')
            if host.wait(timeout=5):
                raise RuntimeError(f'Host exited {host.returncode}')
        finally:
            if host.poll() is None:
                host.kill()
                host.wait()
            socket.unlink(missing_ok=True)


verify([], 0, 'OPENCL_COMPUTE_OK words=64')
verify(['--workgroup'], 0, 'OPENCL_WORKGROUP_OK groups=4')
verify(['--bad-kernel'], 1, 'BUILD_LOG:')
verify(['--expect-mismatch'], 1, 'intentional expectation mismatch')
verify([], 0, 'PUBLIC_OPENCL_IMAGE_MAP_EVENT_BINARY_OK', executable='features')
if os.environ.get('RME_CL_TEST_FP16') == '1':
    verify(['--fp16'], 0, 'PUBLIC_OPENCL_FP16_OK', executable='features')
elif os.environ.get('RME_CL_TEST_FP16') == '0':
    verify(['--no-fp16'], 0, 'PUBLIC_OPENCL_FP16_UNSUPPORTED_OK', executable='features')
if not windows_worker and os.environ.get('RME_CL_TEST_ASYNC_UPLOAD') == '1':
    verify(['--async-upload'], 0, 'PUBLIC_OPENCL_ASYNC_UPLOAD_OK', executable='features',
           host_driver=out / 'delayed-upload.so')
elif windows_worker:
    verify(['--async-upload'], 0, 'PUBLIC_OPENCL_ASYNC_UPLOAD_OK', executable='features')
if os.environ.get('RME_CL_TEST_ASYNC_READ') == '1':
    if not windows_worker:
        env['RME_CL_EXPECT_READ_GATED'] = '1'
    verify(['--async-read'], 0, 'PUBLIC_OPENCL_ASYNC_READ_OK', executable='features',
           host_driver=out / 'delayed-read.so')
if windows_worker and os.environ.get('RME_CL_TEST_EVENT_BATCH'):
    counts = dict(frames=0, separate=0, inline=0)
    status, output = run_windows(['--event-stream'], 'features', counts)
    print(output, counts, flush=True)
    if status or 'PUBLIC_OPENCL_EVENT_STREAM_OK' not in output or counts['separate'] or counts['inline'] != 512:
        raise RuntimeError('Expected all 512 releases to accompany another request')
print('PUBLIC_OPENCL_ABI_OK')
