#!/usr/bin/env python3
"""Regression tests for the standalone OEM-output postprocessor."""
import os
import ctypes
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
TOOL = ROOT / 'native/gueststubs/aod/tools/oem_output.py'
MODEL_PIXELS = 640 * 640


class OfflineOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='rme-aod-offline-core-test-')
        so = Path(cls.build.name) / 'core.so'
        aod = ROOT / 'native/gueststubs/aod'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-std=c11',
                        str(aod / 'tools/oem_output_core.c'), str(aod / 'aod_mask.c'),
                        str(aod / 'aod_postprocess.c'), '-lm', '-o', str(so)], check=True)
        cls.lib = ctypes.CDLL(str(so))
        cls.convert = cls.lib.oem_float_to_u8
        cls.convert.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_size_t,
                                ctypes.POINTER(ctypes.c_uint8)]
        cls.convert.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='rme-aod-offline-test-')
        self.base = Path(self.tmp.name).resolve()
        self.rgb = self.base / 'rgb.raw'
        self.model = self.base / 'model.f32'
        self.rgb.write_bytes(bytes([5, 20, 200]) * (64 * 64))
        self.model.write_bytes(struct.pack('<f', 0.9) * MODEL_PIXELS)
        self.output = self.base / 'new-output'

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, output=None, width='64', height='64'):
        return subprocess.run([sys.executable, str(TOOL), '--rgb', str(self.rgb),
            '--width', width, '--height', height, '--model-output', str(self.model),
            '--out-dir', str(output or self.output)], capture_output=True, text=True)

    def test_success_bgra_alpha_and_highest_match(self):
        p = self.run_tool()
        self.assertEqual(p.returncode, 0, p.stderr)
        alpha = (self.output / 'alpha.raw').read_bytes()
        bgra = (self.output / 'foreground.bgra').read_bytes()
        self.assertEqual(len(alpha), 64 * 64)
        self.assertEqual(len(bgra), 64 * 64 * 4)
        for i, value in enumerate(alpha):
            self.assertEqual(bgra[4*i:4*i+4], bytes([200, 20, 5, value]))
        first = next(i for i, value in enumerate(alpha) if value)
        self.assertEqual((self.output / 'highest.txt').read_text().strip(),
                         f'{first % 64} {first // 64}')

    def test_float_conversion_saturates_and_rounds_nearest_even(self):
        # Cross-checked against OpenCV 5 convertScaleAbs with alpha=255 for the
        # nonnegative values; negative saturation is checked separately.
        values = [-1, 0, .1, .3, .5, .7, .9, 1, 1.2]
        inputs = (ctypes.c_float * len(values))(*values)
        out = (ctypes.c_uint8 * len(values))()
        self.assertEqual(self.convert(inputs, len(values), out), 0)
        self.assertEqual(list(out), [0, 0, 26, 76, 128, 178, 230, 255, 255])
        bad = (ctypes.c_float * 2)(0.5, float('nan'))
        sentinel = (ctypes.c_uint8 * 2)(13, 17)
        self.assertNotEqual(self.convert(bad, 2, sentinel), 0)
        self.assertEqual(list(sentinel), [13, 17])

    def test_rejects_wrong_model_length_nonfinite_and_no_subject(self):
        for content in (b'bad', struct.pack('<f', float('nan')) * MODEL_PIXELS,
                        struct.pack('<f', float('inf')) * MODEL_PIXELS,
                        struct.pack('<f', 0.0) * MODEL_PIXELS):
            with self.subTest(prefix=content[:4]):
                self.model.write_bytes(content)
                self.assertNotEqual(self.run_tool().returncode, 0)
                self.assertFalse(self.output.exists())

    def test_rejects_bad_rgb_oversize_and_existing_directory(self):
        self.rgb.write_bytes(b'bad')
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertNotEqual(self.run_tool(width='99999999999999').returncode, 0)
        self.output.mkdir()
        marker = self.output / 'marker'
        marker.write_text('untouched')
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertEqual(marker.read_text(), 'untouched')

    def test_rejects_repo_and_symlink_to_repo(self):
        inside = ROOT / 'should-not-create-aod-output'
        self.assertNotEqual(self.run_tool(inside).returncode, 0)
        self.assertFalse(inside.exists())
        link = self.base / 'repo-link'
        link.symlink_to(ROOT, target_is_directory=True)
        self.assertNotEqual(self.run_tool(link / inside.name).returncode, 0)
        self.assertFalse(inside.exists())


if __name__ == '__main__':
    unittest.main()
