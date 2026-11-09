import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]
SCRIPT = SOURCE / "build_vibration_engine.py"
STANDARD = SOURCE.parents[2] / "app/src/main/jniLibs/arm64-v8a/libaemu64_36.so"
NAME = "libaemu64_vibration_36.so"


def builder():
    if not SCRIPT.is_file():
        raise AssertionError("vibration-only engine builder missing")
    spec = importlib.util.spec_from_file_location("rme_vibration_engine_builder", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class VibrationEngineBuildTest(unittest.TestCase):
    def test_refuses_normal_engine_name_before_build(self):
        with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-name-") as work:
            output = Path(work) / "libaemu64_36.so"
            output.write_bytes(b"normal-engine")
            result = subprocess.run([sys.executable, str(SCRIPT), "--ndk", "missing", "--output", str(output)],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(NAME, result.stderr)
            self.assertEqual(output.read_bytes(), b"normal-engine")

    def test_publication_is_idempotent_and_does_not_replace_conflict(self):
        publish = builder().publish_built
        with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-publish-") as work:
            directory = Path(work)
            built = directory / "built"
            output = directory / NAME
            built.write_bytes(b"first")
            publish(built, output)
            publish(built, output)
            replacement = directory / "replacement"
            replacement.write_bytes(b"different")
            with self.assertRaises(FileExistsError):
                publish(replacement, output)
            self.assertEqual(output.read_bytes(), b"first")

    def test_refuses_output_symlink(self):
        publish = builder().publish_built
        with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-link-") as work:
            directory = Path(work)
            built = directory / "built"
            other = directory / "other"
            output = directory / NAME
            built.write_bytes(b"value")
            other.write_bytes(b"value")
            try:
                output.symlink_to(other)
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"local symlink creation unavailable: {error}")
            with self.assertRaises(FileExistsError):
                publish(built, output)
            self.assertEqual(other.read_bytes(), b"value")

    @unittest.skipIf(os.name == "nt", "test compiler uses a POSIX shell")
    def test_relative_output_is_resolved_from_invoking_directory(self):
        with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-relative-") as work:
            root = Path(work)
            tools = root / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
            tools.mkdir(parents=True)
            compiler = tools / "clang"
            compiler.write_text('#!/bin/sh\nwhile [ "$1" != "-o" ]; do shift; done\nshift\nprintf built > "$1"\n')
            strip = tools / "llvm-strip"
            strip.write_text('#!/bin/sh\n[ -f "$1" ]\n')
            compiler.chmod(0o755)
            strip.chmod(0o755)
            result = subprocess.run([sys.executable, str(SCRIPT), "--ndk", str(root / "ndk"),
                                     "--output", str(Path("out") / NAME)], cwd=root,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "out" / NAME).read_bytes(), b"built")

    @unittest.skipUnless(os.environ.get("RME_TEST_NDK"), "RME_TEST_NDK not provided")
    def test_real_cross_build_is_reproducible_and_leaves_standard_engine_untouched(self):
        before = hashlib.sha256(STANDARD.read_bytes()).digest()
        with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-cross-") as work:
            outputs = [Path(work) / name / NAME for name in ("first", "second")]
            for index, output in enumerate(outputs):
                output_arg = str(output) if index == 0 else str(Path("second") / NAME)
                result = subprocess.run([sys.executable, str(SCRIPT), "--ndk", os.environ["RME_TEST_NDK"],
                                         "--output", output_arg], cwd=work, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                header = output.read_bytes()[:20]
                self.assertEqual(header[:6], b"\x7fELF\x02\x01")
                self.assertEqual(header[16:20], b"\x02\x00\xb7\x00")  # ET_EXEC, EM_AARCH64
            self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())
        self.assertEqual(hashlib.sha256(STANDARD.read_bytes()).digest(), before)


if __name__ == "__main__":
    unittest.main()
