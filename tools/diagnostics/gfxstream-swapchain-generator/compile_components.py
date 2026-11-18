#!/usr/bin/env python3
"""Cross-compile the matching generated host support sources and state candidate."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('generated', type=Path)
parser.add_argument('state_dir', type=Path)
parser.add_argument('database', type=Path)
parser.add_argument('output', type=Path, help='new directory')
args = parser.parse_args()
generated = args.generated.resolve(strict=True) / 'host'
state_dir = args.state_dir.resolve(strict=True)
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
db = json.loads(args.database.read_text())
names = ['VkDecoderGlobalState.cpp', 'VkDecoderSnapshot.cpp',
         'goldfish_vk_extension_structs.cpp', 'goldfish_vk_marshaling.cpp',
         'goldfish_vk_reserved_marshaling.cpp', 'goldfish_vk_deepcopy.cpp',
         'goldfish_vk_dispatch.cpp', 'goldfish_vk_transform.cpp']

def compile_one(name):
    matches = [e for e in db if e['file'].endswith('/' + name)]
    if len(matches) != 1:
        raise ValueError(f'expected one compile command for {name}, got {len(matches)}')
    entry = matches[0]
    cwd = Path(entry['directory'])
    original = (cwd / entry['file']).resolve(strict=True)
    if name == 'VkDecoderGlobalState.cpp':
        target = state_dir / name
    elif name == 'VkDecoderSnapshot.cpp':
        target = generated / name
    else:
        target = generated / 'cereal/common' / name
    target = target.resolve(strict=True)
    cmd = list(entry.get('arguments') or shlex.split(entry['command']))
    cmd[1:1] = ['-I' + str(state_dir), '-I' + str(generated / 'cereal/common'),
                '-I' + str(generated / 'cereal'), '-I' + str(generated),
                '-I' + str(original.parent)]
    cmd[cmd.index(entry['file'])] = str(target)
    cmd[cmd.index('-o') + 1] = str(out / (name + '.o'))
    if '-MF' in cmd:
        cmd[cmd.index('-MF') + 1] = str(out / (name + '.d'))
    (out / (name + '.command.txt')).write_text(shlex.join(cmd) + '\n')
    with (out / (name + '.log')).open('w') as log:
        result = subprocess.run(cmd, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    return name, result.returncode

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(compile_one, names))
for name, status in results:
    print(name, status, flush=True)
if any(status for _, status in results):
    raise SystemExit(1)
