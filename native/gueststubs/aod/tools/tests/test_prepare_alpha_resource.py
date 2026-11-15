import importlib.util
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

TOOL = Path(__file__).resolve().parents[1] / "prepare_alpha_resource.py"
SPEC = importlib.util.spec_from_file_location("prepare_alpha_resource", TOOL)
resource = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resource)


class PrepareAlphaResourceTest(unittest.TestCase):
    @staticmethod
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))

    def test_grayscale_png_round_trips_all_values(self):
        alpha = bytes([0, 1, 127, 128, 254, 255])
        png = resource.build_png(alpha, 3, 2)
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        resource.validate_png(png, alpha, 3, 2)

    def test_rejects_bad_lengths_and_bad_dimensions(self):
        for width, height, alpha in [(0, 2, b""), (2, 0, b""), (3, 2, b"12345")]:
            with self.subTest(width=width, height=height):
                with self.assertRaises(ValueError):
                    resource.build_png(alpha, width, height)

    def test_rejects_metadata_rgb_corruption_and_trailing_bytes(self):
        alpha = bytes([0, 64, 255, 1, 2, 3])
        png = resource.build_png(alpha, 3, 2)
        ancillary = png[:-12] + self.chunk(b"tEXt", b"author\0private") + png[-12:]
        rgb_header = self.chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 2, 8, 2, 0, 0, 0))
        rgb = png[:8] + rgb_header + png[33:]
        corrupt = png[:-1] + bytes([png[-1] ^ 1])
        for invalid in (ancillary, rgb, corrupt, png[:-1], png + b"extra"):
            with self.subTest(length=len(invalid)):
                with self.assertRaises(ValueError):
                    resource.validate_png(invalid, alpha, 3, 2)

    def test_rejects_different_pixel_values(self):
        alpha = bytes([0, 1, 127, 128, 254, 255])
        png = resource.build_png(alpha, 3, 2)
        with self.assertRaises(ValueError):
            resource.validate_png(png, bytes([0, 1, 127, 128, 254, 0]), 3, 2)

    def test_source_hash_and_length_are_required_and_existing_output_is_preserved(self):
        alpha = b"abcdef"
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "alpha.raw"
            dst = Path(tmp) / "resource.png"
            src.write_bytes(alpha)
            with mock.patch.object(resource, "WIDTH", 3), mock.patch.object(resource, "HEIGHT", 2), \
                    mock.patch.object(resource, "EXPECTED_ALPHA_SHA256", hashlib.sha256(alpha).hexdigest()):
                resource.prepare(src, dst)
                resource.validate_png(dst.read_bytes(), alpha, 3, 2)
                original = dst.read_bytes()
                with self.assertRaises(FileExistsError):
                    resource.prepare(src, dst)
                self.assertEqual(dst.read_bytes(), original)
                dst.unlink()
                src.write_bytes(b"abcdeg")
                with self.assertRaises(ValueError):
                    resource.prepare(src, dst)
                self.assertFalse(dst.exists())
                src.write_bytes(b"short")
                with self.assertRaises(ValueError):
                    resource.prepare(src, dst)
                self.assertFalse(dst.exists())

    def test_existing_resource_can_be_checked_without_modification(self):
        self.assertTrue(hasattr(resource, "verify"), "the existing-resource checker is required")
        alpha = b"abcdef"
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "alpha.raw"
            dst = Path(tmp) / "resource.png"
            src.write_bytes(alpha)
            with mock.patch.object(resource, "WIDTH", 3), mock.patch.object(resource, "HEIGHT", 2), \
                    mock.patch.object(resource, "EXPECTED_ALPHA_SHA256", hashlib.sha256(alpha).hexdigest()):
                resource.prepare(src, dst)
                before = dst.read_bytes()
                self.assertEqual(resource.verify(src, dst), hashlib.sha256(before).hexdigest())
                self.assertEqual(dst.read_bytes(), before)
                dst.write_bytes(before + b"unexpected")
                with self.assertRaises(ValueError):
                    resource.verify(src, dst)


if __name__ == "__main__":
    unittest.main()
