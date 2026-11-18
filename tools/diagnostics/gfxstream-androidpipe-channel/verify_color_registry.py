#!/usr/bin/env python3
"""Cross-compile the real FrameBuffer registry adapter, without a GPU."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('android_build', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
build = args.android_build.resolve(strict=True)
out = args.output.resolve(strict=True)
entries = json.loads((build / 'compile_commands.json').read_text())
entries = [e for e in entries if e['file'].endswith('/FrameBuffer.cpp')]
if len(entries) != 1:
    raise SystemExit('expected one FrameBuffer compile command')
entry = entries[0]
source = (Path(entry['directory']) / entry['file']).resolve(strict=True)
if hashlib.sha256(source.read_bytes()).hexdigest() != 'f1e0fa1f5e5bf034f882159f7bdb14ed7c381afa623bc4c02059a3f493790fa4':
    raise SystemExit('unexpected FrameBuffer source')
argv = shlex.split(entry['command'])
if '-DANDROID' not in argv or argv[-1] != entry['file']:
    raise SystemExit('unexpected Android compile command')
argv[1:1] = ['-I', str(HERE)]
obj = out / 'framebuffer_color_registry-android.o'
for option in ('-o', '-MF', '-MQ'):
    index = argv.index(option)
    argv[index + 1] = str(obj) + ('.d' if option == '-MF' else '')
argv[-1] = str(HERE / 'framebuffer_color_registry.cpp')
subprocess.run(argv, cwd=entry['directory'], check=True, timeout=120)
nm = Path(argv[0]).parent / 'llvm-nm'
symbols = subprocess.check_output([str(nm), '-C', str(obj)], text=True)
if 'U gfxstream::FrameBuffer::findColorBuffer(unsigned int)' not in symbols:
    raise SystemExit('missing actual FrameBuffer registry reference')
print('ANDROID_REAL_FRAMEBUFFER_REGISTRY_OBJECT_OK')
