#!/usr/bin/env python3
"""Exercise extracted d7a process-pipe functions and connection call site."""
import argparse
import pathlib
import subprocess
import tempfile


def extract(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', required=True, type=pathlib.Path)
    p.add_argument('--patch', type=pathlib.Path)
    p.add_argument('--cxx', default='c++')
    p.add_argument('--compiler-arg', action='append', default=[])
    a = p.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    files = ['guest/OpenglSystemCommon/ProcessPipe.cpp', 'guest/OpenglSystemCommon/HostConnection.cpp']
    with tempfile.TemporaryDirectory(prefix='rme-process-pipe-') as tmp:
        root = pathlib.Path(tmp)
        for name in files:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((a.source / name).read_bytes())
        if a.patch:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(a.patch.resolve())], cwd=root, check=True)
        proc = (root / files[0]).read_text()
        host = (root / files[1]).read_text()
        # Preserve the original function bodies; substitutes only implement the surrounding dependencies.
        sections = [extract(proc, 'static QEMU_PIPE_HANDLE sProcPipe', '\n// sProcUID'),
                    extract(proc, 'static void sQemuPipeInit()', '#endif // !__Fuchsia__'),
                    extract(proc, 'static void processPipeDoInit(', '\nuint64_t getPuid()'),
                    extract(proc, 'void processPipeRestart()', '\nvoid refreshHostConnection()')]
        (root / 'process.inc').write_text('\n'.join(sections))
        connect = extract(host, '    auto fd = (connType == HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE)', '\nHostConnection* HostConnection::get()')
        (root / 'connect.inc').write_text(connect)
        subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O0', '-I', str(root),
                        *a.compiler_arg, str(here / 'harness.cpp'), '-o', str(root / 'test')], check=True)
        failed = []
        for case in ('open', 'write', 'read', 'retry', 'normal', 'no_render_control', 'host', 'fd_zero', 'restart_no_pipe'):
            r = subprocess.run([str(root / 'test'), case], capture_output=True, text=True)
            if r.returncode:
                failed.append(f'{case}: exit={r.returncode} {r.stderr.strip()}')
        if failed:
            print('FAILED extracted code:\n' + '\n'.join(failed))
            return 1
        print('PASS extracted process pipe and host call site: 9 cases')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
