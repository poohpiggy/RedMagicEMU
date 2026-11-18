#!/usr/bin/env python3
"""Cross-compile both patched d7a decoder sources with an existing build database."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('database', type=Path)
parser.add_argument('output', type=Path, help='new directory')
parser.add_argument('--patch', type=Path, action='append', required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
relative = Path('host/vulkan')
for name in ('VkDecoderGlobalState.cpp', 'VkDecoderGlobalState.h', 'VkDecoder.cpp'):
    target = out / relative / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((args.source / relative / name).read_bytes())
for patch in args.patch:
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch.resolve())], cwd=out, check=True)
db = json.loads(args.database.read_text())
for name in ('VkDecoderGlobalState.cpp', 'VkDecoder.cpp'):
    found = [entry for entry in db if entry['file'].endswith('/' + name)]
    if len(found) != 1:
        raise ValueError(f'expected exactly one compile entry for {name}')
    entry = found[0]
    cwd = Path(entry['directory'])
    original = (cwd / entry['file']).resolve(strict=True)
    target = out / relative / name
    command = list(entry.get('arguments') or shlex.split(entry['command']))
    command[1:1] = ['-I' + str(target.parent), '-I' + str(original.parent)]
    command[command.index(entry['file'])] = str(target)
    command[command.index('-o') + 1] = str(out / (name + '.o'))
    if '-MF' in command:
        command[command.index('-MF') + 1] = str(out / (name + '.d'))
    (out / (name + '.command.txt')).write_text(shlex.join(command) + '\n')
    with (out / (name + '.log')).open('w') as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('ANDROID_ARM64_COMPILE_OK', name, flush=True)
