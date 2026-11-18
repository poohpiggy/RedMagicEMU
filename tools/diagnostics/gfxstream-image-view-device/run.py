#!/usr/bin/env python3
"""Exercise the fixed d7a image-view handler with a fake driver, entirely offline."""
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
relative = Path('host/vulkan/VkDecoderGlobalState.cpp')
with tempfile.TemporaryDirectory(prefix='rme-view-device-') as tmp:
    root = Path(tmp)
    target = root / relative
    target.parent.mkdir(parents=True)
    target.write_bytes((args.source / relative).read_bytes())
    if args.patch:
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(args.patch.resolve())], cwd=root, check=True)
    source = target.read_text()
    begin = source.index('    VkResult on_vkCreateImageView(')
    end = source.index('    void on_vkDestroyImageView(', begin)
    method = source[begin:end]
    harness = Path(__file__).with_name('harness.cpp').read_text().replace('/* EXTRACTED_METHOD */', method)
    cpp = root / 'test.cpp'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(cpp), '-o', str(root / 'test'), *shlex.split(os.environ.get('LDFLAGS', ''))], check=True)
    subprocess.run([str(root / 'test')], check=True)
