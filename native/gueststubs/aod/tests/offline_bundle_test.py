#!/usr/bin/env python3
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / 'native/gueststubs/aod/tools'


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='rme-aod-bundle-test-')
        self.path = Path(self.tmp.name).resolve()
        self.rgb = self.path / 'source.rgb'
        self.model = self.path / 'model.f32'
        self.rgb.write_bytes(bytes([3, 32, 200]) * (64 * 64))
        self.model.write_bytes(struct.pack('<f', 0.9) * (640 * 640))
        self.derived = self.path / 'derived'
        p = subprocess.run([sys.executable, str(TOOLS / 'oem_output.py'), '--rgb', str(self.rgb),
             '--model-output', str(self.model), '--width', '64', '--height', '64',
             '--out-dir', str(self.derived)], capture_output=True, text=True)
        if p.returncode:
            raise RuntimeError(p.stderr)
        bgra = (self.derived / 'foreground.bgra').read_bytes()
        rows = []
        for y in range(64):
            row = bgra[y*256:(y+1)*256]
            rgba = bytearray(256)
            rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = row[2::4], row[1::4], row[0::4], row[3::4]
            rows.append(b'\x00' + rgba)
        data = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB',64,64,8,6,0,0,0))
        data += png_chunk(b'IDAT', zlib.compress(b''.join(rows))) + png_chunk(b'IEND', b'')
        self.png = self.path / 'foreground.png'
        self.png.write_bytes(data)
        self.highest = self.derived / 'highest.txt'
        self.bundle = self.path / 'bundle'

    def tearDown(self):
        self.tmp.cleanup()

    def command(self, mode, bundle=None):
        return subprocess.run([sys.executable, str(TOOLS / 'oem_bundle.py'), mode,
            '--rgb', str(self.rgb), '--model-output', str(self.model), '--width', '64',
            '--height', '64', '--foreground', str(self.png), '--highest', str(self.highest),
            '--bundle', str(bundle or self.bundle)], capture_output=True, text=True)

    def test_create_verify_and_tamper_rejected(self):
        p = self.command('create')
        self.assertEqual(p.returncode, 0, p.stderr)
        data = json.loads((self.bundle / 'manifest.json').read_text())
        self.assertEqual((data['width'], data['height']), (64, 64))
        self.assertEqual(self.command('verify').returncode, 0)
        (self.bundle / 'foreground.png').write_bytes(b'bad')
        self.assertNotEqual(self.command('verify').returncode, 0)

    def test_wrong_image_and_highest_leave_no_output(self):
        good_png = self.png.read_bytes()
        self.png.write_bytes(b'bad')
        self.assertNotEqual(self.command('create').returncode, 0)
        self.assertFalse(self.bundle.exists())
        self.png.write_bytes(good_png)
        self.highest.write_text('0 123')
        self.assertNotEqual(self.command('create').returncode, 0)
        self.assertFalse(self.bundle.exists())

    def test_wrong_rgb_and_model_leave_no_output(self):
        self.rgb.write_bytes(bytes([3, 33, 200]) * (64 * 64))
        self.assertNotEqual(self.command('create').returncode, 0)
        self.assertFalse(self.bundle.exists())
        self.rgb.write_bytes(bytes([3, 32, 200]) * (64 * 64))
        self.model.write_bytes(struct.pack('<f', 0.0) * (640 * 640))
        self.assertNotEqual(self.command('create').returncode, 0)
        self.assertFalse(self.bundle.exists())

    def test_existing_or_inside_repository_rejected(self):
        self.bundle.mkdir()
        (self.bundle/'marker').write_text('unchanged')
        self.assertNotEqual(self.command('create').returncode, 0)
        self.assertEqual((self.bundle/'marker').read_text(), 'unchanged')
        inside = ROOT / 'no-aod-bundle'
        self.assertNotEqual(self.command('create', inside).returncode, 0)
        self.assertFalse(inside.exists())
        link = self.path / 'repo-link'
        link.symlink_to(ROOT, target_is_directory=True)
        self.assertNotEqual(self.command('create', link / inside.name).returncode, 0)
        self.assertFalse(inside.exists())


if __name__ == '__main__':
    unittest.main()
