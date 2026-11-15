#!/usr/bin/env python3
"""Offline 640x640 OEM model output to raw alpha, BGRA and highest point.

Run on Linux/WSL with Python 3 and a C compiler; no model or image is shipped.
The result uses the checked-in experimental postprocessing, not Android Canvas.
"""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'native/gueststubs/aod'
MODEL_PIXELS = 640 * 640
MAX_DIMENSION = 4096


def exact_read(path, expected):
    with open(path, 'rb') as f:
        data = f.read(expected + 1)
    if len(data) != expected:
        raise ValueError(f'{path}: expected exactly {expected} bytes')
    return data


def run(args):
    if sys.byteorder != 'little':
        raise ValueError('the FLOAT32 input must be little-endian')
    if not (1 <= args.width <= MAX_DIMENSION and 1 <= args.height <= MAX_DIMENSION):
        raise ValueError('image dimensions must be between 1 and 4096')
    pixels = args.width * args.height
    dest = Path(args.out_dir)
    if not dest.is_absolute() or not dest.name:
        raise ValueError('output must be an absolute path to a new directory')
    parent = dest.parent.resolve(strict=True)
    if parent == ROOT or ROOT in parent.parents:
        raise ValueError('output directory must be outside the repository')
    dest = parent / dest.name
    if dest.exists() or dest.is_symlink():
        raise ValueError('output directory already exists')
    rgb = exact_read(args.rgb, pixels * 3)
    model = exact_read(args.model_output, MODEL_PIXELS * 4)

    with tempfile.TemporaryDirectory(prefix='rme-oem-aod-build-') as build:
        libpath = Path(build) / 'core.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-std=c11',
                        str(SOURCE / 'tools/oem_output_core.c'), str(SOURCE / 'aod_mask.c'),
                        str(SOURCE / 'aod_postprocess.c'), '-lm', '-o', str(libpath)], check=True)
        lib = ctypes.CDLL(str(libpath))
        process = lib.oem_process
        process.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                            ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
                            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        process.restype = ctypes.c_int
        input_model = ctypes.create_string_buffer(model)
        input_rgb = ctypes.create_string_buffer(rgb)
        bgra = ctypes.create_string_buffer(pixels * 4)
        x, y = ctypes.c_int(-1), ctypes.c_int(-1)
        if process(input_model, MODEL_PIXELS, input_rgb, len(rgb), args.width, args.height,
                   bgra, pixels * 4, ctypes.byref(x), ctypes.byref(y)):
            raise ValueError('invalid model output or no usable foreground')
        raw = bgra.raw
        alpha = raw[3::4]

    # mkdir is the exclusive claim on the destination; pre-existing paths are never modified.
    dest.mkdir()
    created = []
    try:
        for name, data in [('alpha.raw', alpha), ('foreground.bgra', raw),
                           ('highest.txt', f'{x.value} {y.value}\n'.encode('ascii'))]:
            path = dest / name
            with path.open('xb') as f:
                created.append(path)
                f.write(data)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        dest.rmdir()
        raise
    print(f'created {dest}; highest point: {x.value} {y.value}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rgb', required=True, help='tightly packed RGB8 source')
    p.add_argument('--width', required=True, type=int)
    p.add_argument('--height', required=True, type=int)
    p.add_argument('--model-output', required=True, help='640x640 little-endian FLOAT32')
    p.add_argument('--out-dir', required=True, help='absolute, new directory outside the repository')
    args = p.parse_args()
    try:
        run(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f'oem_output: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
