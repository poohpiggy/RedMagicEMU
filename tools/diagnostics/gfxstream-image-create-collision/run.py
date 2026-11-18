#!/usr/bin/env python3
"""Offline test of the actual d7a image creation handler under raw-image collision."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('--patch', type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
rootdir = here.parent
patches = [rootdir / 'gfxstream-swapchain-metadata/fix.patch',
           rootdir / 'gfxstream-swapchain-images/fix.patch']
if args.patch:
    patches.append(args.patch)
relative = Path('host/vulkan')
with tempfile.TemporaryDirectory(prefix='rme-image-create-conflict-') as temp:
    root = Path(temp)
    for name in ('VkDecoderGlobalState.cpp', 'VkDecoderGlobalState.h', 'VkDecoder.cpp'):
        path = root / relative / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((args.source / relative / name).read_bytes())
    for patch in patches:
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch.resolve())], cwd=root, check=True)
    source = (root / relative / 'VkDecoderGlobalState.cpp').read_text()
    begin = source.index('    VkResult on_vkCreateImage(')
    end = source.index('    void destroyImageLocked(', begin)
    method = source[begin:end]
    code = (here / 'harness.cpp').read_text().replace('/* EXTRACTED_METHOD */', method)
    path = root / 'test.cpp'
    path.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(path), '-o', str(root / 'test'), *shlex.split(os.environ.get('LDFLAGS', ''))], check=True)
    subprocess.run([str(root / 'test')], check=True)
