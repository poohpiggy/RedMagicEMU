#!/usr/bin/env python3
"""Cross-compile the regenerated decoder against the candidate state header."""
import argparse
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
entries = [e for e in json.loads(args.database.read_text()) if e['file'].endswith('/VkDecoder.cpp')]
if len(entries) != 1:
    raise ValueError('expected one decoder compile command')
entry = entries[0]
cwd = Path(entry['directory'])
original = (cwd / entry['file']).resolve(strict=True)
generated = (args.generated / 'host/VkDecoder.cpp').resolve(strict=True)
state_dir = args.state_dir.resolve(strict=True)
if not (state_dir / 'VkDecoderGlobalState.h').is_file():
    raise ValueError('candidate state header not found')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
command = list(entry.get('arguments') or shlex.split(entry['command']))
command[1:1] = ['-I' + str(state_dir), '-I' + str(generated.parent / 'cereal'),
                 '-I' + str(generated.parent), '-I' + str(original.parent)]
if '#include "host_guest_surface.h"' in generated.read_text():
    command[1:1] = ['-I' + str(Path(__file__).resolve().parent.parent / 'gfxstream-surface-wire')]
command[command.index(entry['file'])] = str(generated)
command[command.index('-o') + 1] = str(out / 'VkDecoder.o')
if '-MF' in command:
    command[command.index('-MF') + 1] = str(out / 'VkDecoder.d')
(out / 'command.txt').write_text(shlex.join(command) + '\n')
with (out / 'compile.log').open('w') as log:
    subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
print('ANDROID_ARM64_GENERATED_DECODER_OK', out / 'VkDecoder.o')
