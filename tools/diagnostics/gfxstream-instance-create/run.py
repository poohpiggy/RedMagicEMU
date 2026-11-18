#!/usr/bin/env python3
"""Exercise the extracted upstream instance function with controlled dependencies."""
import argparse
import pathlib
import resource
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=pathlib.Path)
    parser.add_argument('--patch', type=pathlib.Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--compiler-arg', action='append', default=[])
    args = parser.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    original = args.source / 'guest/vulkan/gfxstream_vk_device.cpp'
    with tempfile.TemporaryDirectory(prefix='rme-instance-create-') as tmp:
        root = pathlib.Path(tmp)
        candidate = root / 'guest/vulkan/gfxstream_vk_device.cpp'
        candidate.parent.mkdir(parents=True)
        candidate.write_bytes(original.read_bytes())
        if args.patch:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(args.patch.resolve())], cwd=root, check=True)
        text = candidate.read_text()
        start = text.index('VkResult gfxstream_vk_CreateInstance(')
        end = text.index('\nvoid gfxstream_vk_DestroyInstance(', start)
        (root / 'function.inc').write_text(text[start:end] + '\n')
        subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O0', '-I', str(root),
                        *args.compiler_arg, str(here / 'harness.cpp'), '-o', str(root / 'test')], check=True)
        failures = []
        for case in ('setup', 'connection', 'encoder', 'create', 'init', 'success', 'readonly'):
            result = subprocess.run([str(root / 'test'), case], capture_output=True, text=True,
                                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            if result.returncode:
                failures.append(f'{case}: exit={result.returncode} {result.stderr.strip()}')
        if failures:
            print('FAILED extracted function cases:\n' + '\n'.join(failures))
            return 1
        print('PASS extracted function: setup, connection, encoder, create, init, success, readonly')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
