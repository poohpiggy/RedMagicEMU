import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "build_service.py"
spec = importlib.util.spec_from_file_location("wifi_build_service", SCRIPT)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildServiceTest(unittest.TestCase):
    def test_class_allowlist_requires_model_and_rejects_foreign_classes(self):
        required = [f"Lapp/aemu/stub/{name};" for name in (
            "VirtualWifiState", "WifiStateBinder", "WifiNetworkLifecycle", "VirtualWifiNetworkAgent",
            "WifiSystemContext", "VirtualWifiLinkProperties", "WifiToggleAuthorization", "WifiConnectionCoordinator", "WifiNetworkGate",
            "WifiGuestNetwork", "WifiStub", "WifiNet", "WifiNet2", "WifiService", "WifiServiceGuard", "WifiConfig",
            "WifiLink", "LegacyWifi", "Bcast", "Refl", "WifiPrivacy", "WifiEthernetHandoff", "WifiStateBroadcasts", "WifiScanAccess", "WifiSettingsReader", "WifiControlWaiter", "WifiControlReadiness", "WifiPersistentState",
            "WifiAirplaneModeObserver", "EarlyWifiPublication", "EarlyWifiPublishExperiment", "EarlyWifiProofBinder", "EarlyWifiHandoff", "EarlyHandoffRecovery",
            "WifiDnsGuard", "WifiDnsNetworkSource", "WifiDnsSession", "WifiDnsTransport", "WifiDnsVerifier", "WifiDnsWire", "RadioGuestPeerCredentials")]
        build.validate_service_classes(required + ["Lapp/aemu/stub/VirtualWifiState$Result;"])
        for names in ([], required[:-1], required + ["Landroid/os/Binder;"], required + [required[0]]):
            with self.assertRaises(ValueError):
                build.validate_service_classes(names)

    def test_archive_has_owner_and_is_deterministic(self):
        data = build.build_jar_bytes(b"example dex")
        self.assertEqual(data, build.build_jar_bytes(b"example dex"))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "wifi.jar"
            build.publish_bytes(path, data)
            with self.assertRaises(FileExistsError):
                build.publish_bytes(path, b"replace")
            with zipfile.ZipFile(path) as jar:
                self.assertEqual(set(jar.namelist()), {"classes.dex", build.OWNER_PATH})
                self.assertEqual(jar.read(build.OWNER_PATH), build.OWNER_CONTENT)


if __name__ == "__main__":
    unittest.main()
