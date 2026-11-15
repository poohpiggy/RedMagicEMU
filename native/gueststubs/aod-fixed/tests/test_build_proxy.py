import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "build_proxy.py"
ORIGINAL = b"/system_ext/priv-app/SystemUI_MFV/SystemUI_MFV.apk!/lib/arm64-v8a/libzte_saliency.so"
MASK = b"/system_ext/priv-app/SystemUI_MFV/lib/arm64/rme_aod_mask.raw"


class BuildProxyTest(unittest.TestCase):
    def run_builder(self, ndk, output):
        return subprocess.run([sys.executable, str(SCRIPT), "--ndk", str(ndk), "--output", str(output)],
                              capture_output=True, text=True)

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "proxy.so"
            output.write_bytes(b"preserve")
            result = self.run_builder(Path(tmp) / "missing-ndk", output)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"preserve")
            self.assertIn("output", result.stderr.lower())

    def test_missing_ndk_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "proxy.so"
            result = self.run_builder(Path(tmp) / "missing-ndk", output)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_dangling_output_link_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "proxy.so"
            try:
                output.symlink_to(Path(tmp) / "missing")
            except (NotImplementedError, OSError) as error:
                self.skipTest(f"symlinks unavailable: {error}")
            result = self.run_builder(Path(tmp) / "missing-ndk", output)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(output.is_symlink())

    @unittest.skipUnless(os.environ.get("RME_AOD_NDK"), "RME_AOD_NDK not specified")
    def test_real_ndk_rebuilds_identical_guest_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, second = Path(tmp) / "first.so", Path(tmp) / "second.so"
            one = self.run_builder(os.environ["RME_AOD_NDK"], first)
            self.assertEqual(one.returncode, 0, one.stderr)
            two = self.run_builder(os.environ["RME_AOD_NDK"], second)
            self.assertEqual(two.returncode, 0, two.stderr)
            data = first.read_bytes()
            self.assertEqual(data, second.read_bytes())
            self.assertIn(ORIGINAL + b"\0", data)
            self.assertIn(MASK + b"\0", data)
            self.assertNotIn(b"RME_TEST_ORIGINAL_PATH", data)
            self.assertNotIn(b"/synthetic/original.so", data)
            self.assertNotIn(b"/synthetic/alpha.raw", data)


if __name__ == "__main__":
    unittest.main()
