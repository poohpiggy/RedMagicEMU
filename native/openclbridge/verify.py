"""Compare the same native OpenCL workload directly and over anonymous pipes."""

import argparse
from contextlib import closing
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import statistics
import struct
import subprocess
import time


MAGIC = 0x314C4352
OPEN, BUILD, ALLOC, WRITE, MEMORY_ARG, VALUE_ARG, RUN, READ, RELEASE, RUN_READ = range(1, 11)
REQUEST = struct.Struct('<III')
REPLY = struct.Struct('<IiI')
LIMIT = 1 << 20
PAGE_READWRITE = 4
FILE_MAP_READ = 4
SOURCE = b'''
__kernel void mix(__global const uint* input, __global uint* output, uint rounds) {
    size_t i = get_global_id(0);
    uint value = input[i];
    for (uint j = 0; j < rounds; ++j) {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
    }
    output[i] = value;
}
'''


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


class Direct:
    def __init__(self, directory):
        self.library = C.CDLL(str(directory / 'rme-opencl.dll'))
        driver = C.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32' / 'OpenCL.dll'))
        resolve_type = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p)

        @resolve_type
        def resolve(_, name):
            return C.cast(getattr(driver, name.decode(), None), C.c_void_p).value

        self.driver = driver
        self.library.rme_cl_session_size.restype = C.c_size_t
        self.library.rme_cl_init.argtypes = [C.c_void_p, resolve_type, C.c_void_p]
        self.library.rme_cl_destroy.argtypes = [C.c_void_p]
        self.library.rme_cl_destroy.restype = None
        self.library.rme_cl_call.argtypes = [
            C.c_void_p, C.c_uint32, C.c_char_p, C.c_uint32,
            C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32),
        ]
        self.session = C.create_string_buffer(self.library.rme_cl_session_size())
        self.output = C.create_string_buffer(LIMIT)
        status = self.library.rme_cl_init(self.session, resolve, None)
        if status:
            raise RuntimeError(f'OpenCL symbol loading failed with {status}')

    def request(self, op, payload):
        size = C.c_uint32()
        status = self.library.rme_cl_call(self.session, op, payload, len(payload),
                                          self.output, len(self.output), C.byref(size))
        return status, self.output[:size.value]

    def close(self):
        self.library.rme_cl_destroy(self.session)


class SharedReply:
    def __init__(self):
        self.win = C.WinDLL('kernel32', use_last_error=True)
        self.win.CreateFileMappingW.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, W.DWORD, W.DWORD, W.LPCWSTR]
        self.win.CreateFileMappingW.restype = W.HANDLE
        self.win.MapViewOfFile.argtypes = [W.HANDLE, W.DWORD, W.DWORD, W.DWORD, C.c_size_t]
        self.win.MapViewOfFile.restype = C.c_void_p
        self.win.UnmapViewOfFile.argtypes = [C.c_void_p]
        self.win.CloseHandle.argtypes = [W.HANDLE]
        size = REPLY.size + LIMIT
        self.handle = self.win.CreateFileMappingW(W.HANDLE(-1), None, PAGE_READWRITE, 0, size, None)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        self.address = self.win.MapViewOfFile(self.handle, FILE_MAP_READ, 0, 0, size)
        if not self.address:
            error = C.WinError(C.get_last_error())
            self.win.CloseHandle(self.handle)
            raise error

    def read(self, size):
        return C.string_at(self.address + REPLY.size, size)

    def close(self):
        self.win.UnmapViewOfFile(self.address)
        self.win.CloseHandle(self.handle)


class Pipe:
    def __init__(self, directory, shared=False):
        self.reply = SharedReply() if shared else None
        handle = self.reply.handle if self.reply else 0
        startup = subprocess.STARTUPINFO()
        if self.reply:
            startup.lpAttributeList = {'handle_list': [handle]}
            os.set_handle_inheritable(handle, True)
        try:
            self.process = subprocess.Popen([str(directory / 'rme-opencl-worker.exe')],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, startupinfo=startup)
        except OSError:
            if self.reply:
                self.reply.close()
            raise
        if self.reply:
            os.set_handle_inheritable(handle, False)
        self.process.stdin.write(struct.pack('<Q', handle))
        self.process.stdin.flush()

    def request(self, op, payload):
        self.process.stdin.write(REQUEST.pack(MAGIC, op, len(payload)) + payload)
        self.process.stdin.flush()
        header = self.process.stdout.read(REPLY.size)
        if len(header) != REPLY.size:
            raise RuntimeError(f'Incomplete worker reply, exit status {self.process.poll()}')
        magic, status, size = REPLY.unpack(header)
        if magic != MAGIC or size > LIMIT:
            raise RuntimeError(f'Invalid worker reply {magic:#x}, {size} bytes')
        response = self.reply.read(size) if self.reply else self.process.stdout.read(size)
        if len(response) != size:
            raise RuntimeError(f'Incomplete worker payload, expected {size} bytes')
        return status, response

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()
        if self.reply:
            self.reply.close()
        try:
            status = self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        if status:
            raise RuntimeError(f'Worker exited with {self.process.returncode}')


def call(endpoint, op, payload=b''):
    status, data = endpoint.request(op, payload)
    if status:
        raise RuntimeError(f'Operation {op} returned {status}\n{data.decode(errors="replace")}')
    return data


def setup(endpoint, device):
    identity = call(endpoint, OPEN, device.encode() + b'\0')
    kernel = b'mix\0'
    call(endpoint, BUILD, words(len(kernel)) + kernel + SOURCE)
    status, log = endpoint.request(BUILD, words(len(kernel)) + kernel + b'__kernel void {')
    if status != -11 or not log.strip(b'\0\n '):
        raise RuntimeError(f'Expected driver compiler error and log, got {status}: {log!r}')
    return identity


def configure(endpoint, data, rounds):
    for slot in range(2):
        call(endpoint, ALLOC, words(slot, len(data)))
    call(endpoint, WRITE, words(0, 0) + data)
    call(endpoint, MEMORY_ARG, words(0, 0))
    call(endpoint, MEMORY_ARG, words(1, 1))
    call(endpoint, VALUE_ARG, words(2, rounds))


def sample(endpoint, fused, count, expected):
    start = time.perf_counter_ns()
    if fused:
        response = call(endpoint, RUN_READ, words(count, 1, 0, len(expected)))
        timing, output = response[:8], response[8:]
    else:
        timing = call(endpoint, RUN, words(count))
        output = call(endpoint, READ, words(1, 0, len(expected)))
    wall_ns = time.perf_counter_ns() - start
    gpu_ns, = struct.unpack('<Q', timing)
    if output != expected:
        raise RuntimeError('Readback differs from the reference output')
    if gpu_ns <= 0:
        raise RuntimeError(f'Invalid GPU event duration {gpu_ns}')
    return wall_ns, gpu_ns


def reference(inputs, rounds):
    mask = (1 << 32) - 1
    # XOR and shifts are linear over bits. Evaluate the single-bit inputs and
    # compose them to verify all work-items independently of the OpenCL output.
    basis = []
    for bit in range(32):
        value = 1 << bit
        for _ in range(rounds):
            value ^= (value << 13) & mask
            value ^= value >> 17
            value ^= (value << 5) & mask
        basis.append(value)
    result = []
    for value in inputs:
        transformed = 0
        while value:
            bit = value & -value
            transformed ^= basis[bit.bit_length() - 1]
            value ^= bit
        result.append(transformed)
    return words(*result)


def measure(endpoints, count, rounds, samples):
    inputs = [(index * 2654435761) & 0xffffffff for index in range(count)]
    data = words(*inputs)
    expected = reference(inputs, rounds)
    modes = [('direct', False), ('pipe', False), ('direct', True), ('pipe', True), ('shared', True)]
    observations = {mode: [] for mode in modes}
    for endpoint in endpoints.values():
        configure(endpoint, data, rounds)
    for group in range(len(modes)):
        for name, fused in modes[group:] + modes[:group]:
            endpoint = endpoints[name]
            for _ in range(8):
                sample(endpoint, fused, count, expected)
            observations[name, fused].extend(sample(endpoint, fused, count, expected) for _ in range(samples))
    metrics = {
        name: tuple(statistics.median(values) / 1000 for values in zip(*results))
        for name, results in observations.items()
    }
    for endpoint in endpoints.values():
        call(endpoint, RELEASE, words(1))
        call(endpoint, RELEASE, words(0))
        status, _ = endpoint.request(READ, words(1, 0, 4))
        if status != -38:
            raise RuntimeError(f'Released buffer returned status {status}')
    print(f'{count} work-items, {rounds} rounds, {len(expected)} bytes verified, '
          f'{samples * len(modes)} samples per path')
    for label, path, fused in [('two requests', 'pipe', False), ('one request', 'pipe', True),
                               ('shared reply', 'shared', True)]:
        direct, bridge = metrics['direct', fused], metrics[path, fused]
        print(f'  {label:12} | wall direct {direct[0]:8.1f} us, bridge {bridge[0]:8.1f} us'
              f' ({bridge[0] - direct[0]:+.1f} us, {bridge[0] / direct[0]:.3f}x)'
              f' | GPU direct {direct[1]:.1f} us, bridge {bridge[1]:.1f} us ({bridge[1] / direct[1]:.3f}x)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--device', default='NVIDIA GeForce RTX 3080')
    parser.add_argument('--samples', type=int, default=20)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('--samples must be positive')
    directory = args.build.resolve()
    with (closing(Direct(directory)) as direct, closing(Pipe(directory)) as pipe,
          closing(Pipe(directory, shared=True)) as shared):
        endpoints = {'direct': direct, 'pipe': pipe, 'shared': shared}
        identities = [setup(endpoint, args.device) for endpoint in endpoints.values()]
        if any(identity != identities[0] for identity in identities[1:]):
            raise RuntimeError(f'Different OpenCL drivers selected: {identities}')
        print(identities[0].rstrip(b'\0').decode().replace('\0', ' | driver '), flush=True)
        measure(endpoints, 64, 16, args.samples)
        measure(endpoints, 16384, 128, args.samples)
        measure(endpoints, 65536, 4096, args.samples)
        measure(endpoints, 65536, 32768, args.samples)
    print('NATIVE_OPENCL_COMPUTE_OK')


if __name__ == '__main__':
    main()
