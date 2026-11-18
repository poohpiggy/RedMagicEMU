#!/usr/bin/env python3
"""Cross-compile the modified real HostConnection with the paired encoder header."""
import argparse
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser()
p.add_argument('template', type=Path)
p.add_argument('source', type=Path)
p.add_argument('headers', type=Path)
p.add_argument('encoder', type=Path)
p.add_argument('connection', type=Path)
p.add_argument('output', type=Path, help='new directory')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
args = shlex.split(a.template.read_text())
args = [x for x in args if '/android_stub' not in x and not x.startswith('-DLOG_TAG=') and x != '-Werror']
source = a.source.resolve(strict=True)
includes = [a.encoder.resolve(strict=True), a.connection.resolve(strict=True).parent,
            a.headers.resolve(strict=True), source / 'guest/mesa/src/vulkan/wsi',
            source / 'common/etc/include', source / 'guest/platform/include',
            source / 'guest/OpenglSystemCommon']
drm = [Path(x[2:]) for x in args if x.startswith('-I') and x.endswith('/include/drm')]
if drm:
    includes.append(drm[0].parent.parent)
base = [Path(x[2:]) for x in args if x.startswith('-I') and x.endswith('/aosp-libbase-files/include')]
if base:
    aosp = base[0].parent.parent
    includes += [aosp / x for x in ('aosp-nativewindow', 'aosp-nativebase', 'aosp-minigbm', 'aosp-core/libsystem/include')]
args[1:1] = ['-DLOG_TAG="eglCodecCommon"'] + ['-I' + str(x) for x in includes]
args[args.index('-c') + 1] = str(a.connection.resolve(strict=True))
args[args.index('-o') + 1] = str(out / 'HostConnection.o')
args[args.index('-MF') + 1] = str(out / 'HostConnection.d')
(out / 'command.txt').write_text(shlex.join(args) + '\n')
result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'compile.log').write_text(result.stdout)
if result.returncode:
    print(result.stdout)
    raise SystemExit(result.returncode)
if 'android_stub/' in (out / 'HostConnection.d').read_text():
    raise SystemExit('unexpected Mesa stub headers')
print('ANDROID_WSI_NEGOTIATED_HOST_CONNECTION_OK', out / 'HostConnection.o')
