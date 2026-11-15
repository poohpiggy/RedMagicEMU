#!/usr/bin/env python3
"""Create or verify a local precomputed foreground bundle; no Guest changes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[4]
TOOL_DIR = Path(__file__).resolve().parent
SOURCES = [TOOL_DIR / 'oem_output.py', TOOL_DIR / 'oem_output_core.c',
           TOOL_DIR.parent / 'aod_mask.c', TOOL_DIR.parent / 'aod_mask.h',
           TOOL_DIR.parent / 'aod_postprocess.c', TOOL_DIR.parent / 'aod_postprocess.h']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def png_pixels_match(data, bgra, width, height):
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('foreground is not PNG')
    pos, chunks, seen_header, seen_end = 8, [], False, False
    while pos < len(data):
        if len(data) - pos < 12:
            raise ValueError('truncated PNG chunk')
        length = struct.unpack_from('>I', data, pos)[0]
        kind = data[pos+4:pos+8]
        end = pos + 12 + length
        if end > len(data):
            raise ValueError('truncated PNG data')
        payload = data[pos+8:pos+8+length]
        crc = struct.unpack_from('>I', data, pos+8+length)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError('PNG CRC mismatch')
        if kind == b'IHDR':
            if seen_header or pos != 8 or length != 13:
                raise ValueError('invalid PNG header')
            if struct.unpack('>IIBBBBB', payload) != (width, height, 8, 6, 0, 0, 0):
                raise ValueError('PNG must be noninterlaced RGBA8 with the source dimensions')
            seen_header = True
        elif kind == b'IDAT':
            if not seen_header or seen_end:
                raise ValueError('invalid PNG chunk order')
            chunks.append(payload)
        elif kind == b'IEND':
            if length or end != len(data):
                raise ValueError('invalid PNG end')
            seen_end = True
        elif kind[0] & 32 == 0:
            raise ValueError('unsupported critical PNG chunk')
        pos = end
    if not seen_header or not seen_end or not chunks:
        raise ValueError('incomplete PNG')
    size = (width * 4 + 1) * height
    d = zlib.decompressobj()
    decoded = d.decompress(b''.join(chunks), size + 1)
    if len(decoded) != size or not d.eof or d.unused_data or d.unconsumed_tail:
        raise ValueError('invalid PNG image data')
    stride = width * 4
    prior = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = decoded[start]
        row = bytearray(decoded[start+1:start+1+stride])
        if kind not in (0, 1, 2, 3, 4):
            raise ValueError('invalid PNG filter')
        if kind == 1:
            for i in range(4, stride): row[i] = (row[i] + row[i-4]) & 255
        elif kind == 2:
            for i in range(stride): row[i] = (row[i] + prior[i]) & 255
        elif kind == 3:
            for i in range(stride): row[i] = (row[i] + ((row[i-4] if i >= 4 else 0) + prior[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                a, b, c = (row[i-4] if i >= 4 else 0), prior[i], (prior[i-4] if i >= 4 else 0)
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[i] = (row[i] + pred) & 255
        src = bgra[y*stride:(y+1)*stride]
        if row[0::4] != src[2::4] or row[1::4] != src[1::4] or row[2::4] != src[0::4] or row[3::4] != src[3::4]:
            raise ValueError('foreground PNG pixels differ from recomputed BGRA')
        prior = row


def run(args):
    if not (1 <= args.width <= 4096 and 1 <= args.height <= 4096):
        raise ValueError('invalid source dimensions')
    path = Path(args.bundle)
    if not path.is_absolute() or not path.name:
        raise ValueError('bundle path must be absolute')
    parent = path.parent.resolve(strict=True)
    if parent == ROOT or ROOT in parent.parents:
        raise ValueError('bundle must be outside the repository')
    bundle = parent / path.name
    if args.mode == 'create' and (bundle.exists() or bundle.is_symlink()):
        raise ValueError('bundle path already exists')
    if args.mode == 'verify' and (bundle.is_symlink() or not bundle.is_dir()):
        raise ValueError('bundle must be an existing directory')

    with tempfile.TemporaryDirectory(prefix='rme-aod-bundle-') as tmp:
        stage = Path(tmp)
        files = []
        for name, source, limit in [('source.rgb', args.rgb, args.width*args.height*3),
                                   ('model.f32', args.model_output, 640*640*4),
                                   ('foreground.png', args.foreground, args.width*args.height*8+1048576),
                                   ('highest.txt', args.highest, 256)]:
            target = stage / name
            with open(source, 'rb') as inp, target.open('xb') as out:
                data = inp.read(limit + 1)
                if len(data) > limit: raise ValueError(f'{name} exceeds expected length')
                out.write(data)
            files.append(data)
        rgb, model, foreground, highest = files
        result = stage / 'derived'
        subprocess.run([sys.executable, str(TOOL_DIR/'oem_output.py'), '--rgb', str(stage/'source.rgb'),
                        '--model-output', str(stage/'model.f32'), '--width', str(args.width),
                        '--height', str(args.height), '--out-dir', str(result)], check=True,
                       stdout=subprocess.DEVNULL)
        expected_highest = (result/'highest.txt').read_bytes()
        try:
            point = tuple(int(x) for x in highest.decode('ascii').split())
        except (UnicodeDecodeError, ValueError) as exc:
            raise ValueError('invalid highest point') from exc
        if len(point) != 2 or point != tuple(int(x) for x in expected_highest.split()):
            raise ValueError('highest point differs from recomputed result')
        png_pixels_match(foreground, (result/'foreground.bgra').read_bytes(), args.width, args.height)
        manifest = {'format': 'redmagicemu-oem-aod-foreground', 'version': 1,
                    'width': args.width, 'height': args.height, 'highest': list(point),
                    'sha256': {'source_rgb': sha(rgb), 'model_float32': sha(model),
                               'foreground_png': sha(foreground), 'highest_txt': sha(expected_highest),
                               'alpha_raw': sha((result/'alpha.raw').read_bytes()),
                               'foreground_bgra': sha((result/'foreground.bgra').read_bytes())},
                    'processing_sources_sha256': {p.name: sha(p.read_bytes()) for p in SOURCES}}
        manifest_data = (json.dumps(manifest, ensure_ascii=True, sort_keys=True, indent=2)+'\n').encode()
        if args.mode == 'verify':
            if json.loads((bundle/'manifest.json').read_text()) != manifest:
                raise ValueError('manifest differs from inputs or processing sources')
            if (bundle/'foreground.png').read_bytes() != foreground or (bundle/'highest.txt').read_bytes() != expected_highest:
                raise ValueError('bundle asset differs from validated input')
            print(f'verified {bundle}')
            return
        bundle.mkdir()
        created = []
        try:
            for name, data in [('foreground.png', foreground), ('highest.txt', expected_highest), ('manifest.json', manifest_data)]:
                file = bundle/name
                with file.open('xb') as out:
                    created.append(file)
                    out.write(data)
        except BaseException:
            for file in created: file.unlink(missing_ok=True)
            bundle.rmdir()
            raise
        print(f'created {bundle}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['create', 'verify'])
    for name in ('rgb', 'model-output', 'foreground', 'highest', 'bundle'):
        p.add_argument('--'+name, required=True)
    p.add_argument('--width', required=True, type=int)
    p.add_argument('--height', required=True, type=int)
    try:
        run(p.parse_args())
    except (OSError, ValueError, zlib.error, subprocess.CalledProcessError) as e:
        print(f'oem_bundle: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
