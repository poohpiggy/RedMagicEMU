#!/usr/bin/env python3
"""Compare the API-36 ARM64 guest GLES bridge with a temporary source build.

The compiler and strip options match the ARM64 guest target in build.sh. This
uses the existing generated headers and never writes an asset or runs gen.py.
Matching bytes establish build consistency, not correctness or device behavior.
"""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ndk', type=Path, required=True)
    args = parser.parse_args()
    source_dir = Path(__file__).resolve().parent.parent
    repo = source_dir.parent.parent
    asset = repo / 'app/src/main/assets/engines/n64-36/system#lib64#egl#libGLES_aemu.so'
    host = 'windows-x86_64' if sys.platform == 'win32' else 'linux-x86_64'
    ext = '.exe' if sys.platform == 'win32' else ''
    tools = args.ndk.resolve() / 'toolchains' / 'llvm' / 'prebuilt' / host / 'bin'
    with tempfile.TemporaryDirectory(prefix='rme-gles-asset-') as temp:
        built = Path(temp) / 'libGLES_aemu.so'
        command = [str(tools / ('clang' + ext)), '--target=aarch64-linux-android21',
                   '-O2', '-fPIC', '-shared', '-fvisibility=hidden', '-Wall', '-Wno-unused-function',
                   '-Wl,-soname,libGLES_aemu.so', '-o', str(built), 'guest.c', '-llog', '-ldl']
        subprocess.run(command, cwd=source_dir, check=True)
        subprocess.run([str(tools / ('llvm-strip' + ext)), str(built)], check=True)
        actual = asset.read_bytes()
        expected = built.read_bytes()
        print('asset  SHA-256:', hashlib.sha256(actual).hexdigest())
        print('source SHA-256:', hashlib.sha256(expected).hexdigest())
        if actual != expected:
            print('FAIL: API-36 ARM64 GLES guest asset differs from source build', file=sys.stderr)
            return 1
    print('GLES_ASSET_REPRODUCIBLE_OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
