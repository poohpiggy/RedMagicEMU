import importlib.util
import io
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "build_service.py"


def load_builder():
    if not SCRIPT.is_file():
        raise AssertionError("standalone radio service builder is missing")
    spec = importlib.util.spec_from_file_location("build_radio_service", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ServiceArtifactBoundaryTest(unittest.TestCase):
    def test_exact_service_sources_and_only_required_bridge_sources(self):
        builder = load_builder()
        paths = builder.service_sources()
        expected = {"RadioServiceMain", "RadioSlot1Registry", "RadioVoiceService", "RadioRequestCounter",
                    "RadioConfigService", "SyntheticSingleSlot", "SyntheticSimProfile", "RadioModemService",
                    "RadioSimService", "RadioNetworkService", "RadioDataService", "RadioMessagingService",
                    "RadioEventEndpoint", "RadioEventSession", "RadioEventSessionManager", "RadioGuestPeerCredentials",
                    "RadioEventSessionPump", "RadioEventSocketTransport"}
        self.assertEqual({path.stem for path in paths}, expected)
        self.assertEqual(set(builder.SERVICE_CLASSES), expected)
        self.assertEqual(len(paths), len(expected))
        self.assertTrue(all("tests" not in path.parts for path in paths))
        self.assertEqual({p.stem for p in paths if "radio-bridge" in p.parts},
                         {"RadioEventEndpoint", "RadioEventSession", "RadioEventSessionManager", "RadioGuestPeerCredentials",
                          "RadioEventSessionPump", "RadioEventSocketTransport"})

    def test_dex_requires_every_service_and_rejects_foreign_classes(self):
        builder = load_builder()
        names = ["Lapp/aemu/stub/" + name + ";" for name in builder.SERVICE_CLASSES]
        builder.validate_service_classes(names + ["Lapp/aemu/stub/RadioVoiceService$Callbacks;"])
        for missing in ("RadioServiceMain", "RadioSlot1Registry", "RadioVoiceService", "RadioEventSessionPump", "RadioEventSocketTransport"):
            with self.assertRaises(ValueError):
                builder.validate_service_classes([n for n in names if n != "Lapp/aemu/stub/" + missing + ";"])
        for forbidden in ("Landroid/os/Binder;", "Lapp/aemu/stub/RadioVoiceContractTest;",
                          "Lapp/aemu/stub/RadioSyntheticRegistry;",
                          "Lapp/aemu/stub/RadioVoiceService$Invalid/Foreign;"):
            with self.assertRaises(ValueError):
                builder.validate_service_classes(names + [forbidden])
        with self.assertRaises(ValueError):
            builder.validate_service_classes(names + [names[0]])

    def test_output_creation_is_exclusive(self):
        builder = load_builder()
        with tempfile.TemporaryDirectory(prefix="rme-radio-service-output-test-") as work:
            output = Path(work) / "service.jar"
            builder.publish_bytes(output, b"first")
            with self.assertRaises(FileExistsError):
                builder.publish_bytes(output, b"second")
            self.assertEqual(output.read_bytes(), b"first")

    def test_archive_has_fixed_owner_marker_and_only_one_code_entry(self):
        builder = load_builder()
        first = builder.build_jar_bytes(b"synthetic-dex")
        self.assertEqual(first, builder.build_jar_bytes(b"synthetic-dex"))
        with zipfile.ZipFile(io.BytesIO(first)) as jar:
            self.assertEqual(jar.namelist(), ["classes.dex", "META-INF/redmagic-radio-owner"])
            self.assertEqual(jar.read("classes.dex"), b"synthetic-dex")
            self.assertEqual(jar.read("META-INF/redmagic-radio-owner"), b"RedMagicEMU radio-service v1\n")

    def test_cli_requires_explicit_output(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--sdk", "unused"],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--output", result.stderr)

    def test_cli_refuses_existing_file_before_build(self):
        with tempfile.TemporaryDirectory(prefix="rme-radio-service-cli-test-") as work:
            output = Path(work) / "existing.jar"
            output.write_bytes(b"preserve")
            result = subprocess.run([sys.executable, str(SCRIPT), "--sdk", "unused", "--output", str(output)],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output already exists", result.stderr)
            self.assertEqual(output.read_bytes(), b"preserve")

    def test_output_creation_rejects_dangling_link(self):
        builder = load_builder()
        with tempfile.TemporaryDirectory(prefix="rme-radio-service-link-test-") as work:
            output = Path(work) / "link.jar"
            target = Path(work) / "missing.jar"
            try:
                output.symlink_to(target)
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"local symlink creation unavailable: {error}")
            with self.assertRaises(FileExistsError):
                builder.publish_bytes(output, b"not written")
            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
