import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / "build_network.py"
spec = importlib.util.spec_from_file_location("wifi_network_build", SCRIPT)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildNetworkTest(unittest.TestCase):
    def test_rejects_missing_unrelated_and_duplicate_classes(self):
        names = [f"Lapp/aemu/stub/{name};" for name in ("NetStub", "NetAgent2", "TargetNetControl")]
        build.validate_classes(names + ["Lapp/aemu/stub/NetStub$1;"])
        for invalid in ([], names[:-1], names + ["Landroid/os/Binder;"], names + [names[0]]):
            with self.assertRaises(ValueError):
                build.validate_classes(invalid)

    def test_owned_archive_is_deterministic_and_no_overwrite(self):
        data = build.jar_bytes(b"dex")
        self.assertEqual(data, build.jar_bytes(b"dex"))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "net.jar"
            build.publish_bytes(path, data)
            with self.assertRaises(FileExistsError):
                build.publish_bytes(path, b"replace")
            with zipfile.ZipFile(path) as jar:
                self.assertEqual(set(jar.namelist()), {"classes.dex", build.OWNER_PATH})
                self.assertEqual(jar.read(build.OWNER_PATH), build.OWNER_CONTENT)


if __name__ == "__main__":
    unittest.main()
