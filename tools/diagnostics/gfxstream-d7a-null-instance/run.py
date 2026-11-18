#!/usr/bin/env python3
"""Exercise the extracted gfxstream GetInstanceProcAddr with UBSan."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def extract(source):
    signature = 'PFN_vkVoidFunction gfxstream_vk_GetInstanceProcAddr(VkInstance _instance, const char* pName) {'
    start = source.find(signature)
    if start < 0 or source.find(signature, start + 1) >= 0:
        raise ValueError('expected one GetInstanceProcAddr definition')
    depth = 0
    for end in range(source.find('{', start), len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[start:end + 1] + '\n'
    raise ValueError('unterminated function')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--patch', type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    original = args.source / 'guest/vulkan/gfxstream_vk_device.cpp'
    with tempfile.TemporaryDirectory(prefix='rme-null-instance-') as tmp:
        root = Path(tmp)
        candidate = root / 'guest/vulkan/gfxstream_vk_device.cpp'
        candidate.parent.mkdir(parents=True)
        candidate.write_bytes(original.read_bytes())
        if args.patch:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(args.patch.resolve())],
                           cwd=root, check=True)
        (root / 'function.h').write_text(extract(candidate.read_text(encoding='utf-8')), encoding='utf-8')
        binary = root / 'test'
        subprocess.run([args.cxx, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                        '-fno-sanitize-link-runtime', '-nostdlib++',
                        '-I', str(root), str(here / 'harness.cpp'), '-lubsan', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS actual GetInstanceProcAddr: null global and non-null instance (fake dispatcher)')


if __name__ == '__main__': main()
