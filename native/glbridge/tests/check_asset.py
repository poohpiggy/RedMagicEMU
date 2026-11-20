#!/usr/bin/env python3
"""Compare the API-36 ARM64 Guest and host GLES libraries with source builds.

The compiler and strip options match the two ARM64 targets in build.sh. This
uses the existing generated headers and writes the builds to a temporary directory.
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
    toolchain = 'windows-x86_64' if sys.platform == 'win32' else 'linux-x86_64'
    ext = '.exe' if sys.platform == 'win32' else ''
    tools = args.ndk.resolve() / 'toolchains' / 'llvm' / 'prebuilt' / toolchain / 'bin'
    artifacts = (
        ('guest.c', 'libGLES_aemu.so',
         'app/src/main/assets/engines/n64-36/system#lib64#egl#libGLES_aemu.so',
         '21', ('-fvisibility=hidden',), ('-llog', '-ldl')),
        ('host.c', 'libaemugl.so', 'app/src/main/jniLibs/arm64-v8a/libaemugl.so',
         '29', (), ('-lEGL', '-lGLESv3', '-llog', '-lnativewindow', '-landroid')),
    )
    mismatch = False
    with tempfile.TemporaryDirectory(prefix='rme-gles-asset-') as temp:
        for source, name, path, api, options, libraries in artifacts:
            built = Path(temp) / name
            command = [str(tools / ('clang' + ext)), '--target=aarch64-linux-android' + api,
                       '-O2', '-fPIC', '-shared', *options, '-Wall', '-Wno-unused-function',
                       '-Wl,-soname,' + name, '-o', str(built), source, *libraries]
            subprocess.run(command, cwd=source_dir, check=True)
            subprocess.run([str(tools / ('llvm-strip' + ext)), str(built)], check=True)
            actual = (repo / path).read_bytes()
            expected = built.read_bytes()
            print(name, 'asset  SHA-256:', hashlib.sha256(actual).hexdigest())
            print(name, 'source SHA-256:', hashlib.sha256(expected).hexdigest())
            if actual != expected:
                print(f'FAIL: {name} differs from the ARM64 source build', file=sys.stderr)
                mismatch = True
    if mismatch:
        return 1
    print('GLES_ASSET_REPRODUCIBLE_OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
