#!/usr/bin/env python3
"""Compile the optional adapter against a same-revision generated VkEncoder."""
import argparse
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('encoder_command', type=Path)
parser.add_argument('generated_guest', type=Path)
parser.add_argument('output', type=Path, help='new output directory')
parser.add_argument('--experimental', action='store_true', help='also compile the negotiated-channel provider')
parser.add_argument('--wsi-negotiation', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
generated = args.generated_guest.resolve(strict=True)
if 'vkCreateEmuSurfaceRME' not in (generated / 'VkEncoder.h').read_text():
    raise SystemExit('generated private surface encoder header required')
if args.wsi_negotiation and 'rmeWsiEnabled()' not in (generated / 'VkEncoder.h').read_text():
    raise SystemExit('same-channel WSI negotiation encoder header required')
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
command = shlex.split(args.encoder_command.read_text())
command[1:1] = ['-I' + str(generated), '-I' + str(here)]
if args.experimental:
    command.insert(1, '-DRME_EXPERIMENTAL_REMOTE_SURFACE')
if args.wsi_negotiation:
    command.insert(1, '-DRME_EXPERIMENTAL_REMOTE_SURFACE')
command[command.index('-c') + 1] = str(here / 'android_remote_surface_encoder.cpp')
command[command.index('-o') + 1] = str(output / 'remote.o')
command[command.index('-MF') + 1] = str(output / 'remote.d')
(output / 'command.txt').write_text(shlex.join(command) + '\n')
subprocess.run(command, check=True)
print('ANDROID_REMOTE_SURFACE_ENCODER_OBJECT_OK', output / 'remote.o')
