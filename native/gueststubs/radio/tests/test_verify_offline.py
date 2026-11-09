import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "verify_offline.py"
SPEC = importlib.util.spec_from_file_location("verify_offline", SCRIPT)
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class FrozenSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rme-radio-snapshot-test-")
        root = Path(self.temporary.name)
        self.sources = {}
        for name, source in VERIFY.SOURCES.items():
            destination = root / name
            shutil.copytree(source, destination)
            self.sources[name] = destination
        self.manifest = SCRIPT.parent / "aidl-sha256.json"

    def tearDown(self):
        self.temporary.cleanup()

    def test_intact_snapshot_passes(self):
        VERIFY.verify_frozen_sources(self.sources, self.manifest)

    def test_single_byte_tamper_fails_and_restoring_passes(self):
        path = self.sources["base"] / "android/hardware/radio/RadioError.aidl"
        original = path.read_bytes()
        changed = bytearray(original)
        changed[0] ^= 1
        path.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            VERIFY.verify_frozen_sources(self.sources, self.manifest)
        path.write_bytes(original)
        VERIFY.verify_frozen_sources(self.sources, self.manifest)

    def test_missing_or_added_source_fails(self):
        path = self.sources["base"] / "android/hardware/radio/RadioError.aidl"
        path.unlink()
        with self.assertRaisesRegex(ValueError, "file set"):
            VERIFY.verify_frozen_sources(self.sources, self.manifest)
        path.write_bytes(b"unexpected")
        (path.parent / "Unexpected.aidl").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "file set"):
            VERIFY.verify_frozen_sources(self.sources, self.manifest)

    def test_version_hash_tamper_fails(self):
        path = self.sources["voice"] / ".hash"
        path.write_bytes(b"0" * 40 + b"\n")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            VERIFY.verify_frozen_sources(self.sources, self.manifest)


class JdkEnvironmentTest(unittest.TestCase):
    def test_explicit_java_home_overrides_incorrect_environment(self):
        chosen = Path(tempfile.gettempdir()) / "rme-selected-jdk"
        with mock.patch.dict(os.environ, {"JAVA_HOME": "incorrect-jdk", "PATH": "old-path"}, clear=True):
            environment = VERIFY.jdk_environment(chosen)
            self.assertEqual(environment["JAVA_HOME"], str(chosen.resolve()))
            self.assertEqual(environment["PATH"], str(chosen.resolve() / "bin") + os.pathsep + "old-path")
            self.assertEqual(os.environ["JAVA_HOME"], "incorrect-jdk")

    def test_inherited_environment_remains_when_no_explicit_jdk(self):
        with mock.patch.dict(os.environ, {"JAVA_HOME": "inherited", "PATH": "old-path"}, clear=True):
            environment = VERIFY.jdk_environment(None)
            self.assertEqual(environment["JAVA_HOME"], "inherited")
            self.assertEqual(environment["PATH"], "old-path")


if __name__ == "__main__":
    unittest.main()
