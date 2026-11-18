"""WSL socket protocol tests using a fake loader, never a real GPU.

Run: python3 native/opencl-host-probe/tests/test_discovery.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "discovery.c"


class DiscoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="rme-opencl-discovery-")
        cls.root = Path(cls.temp.name)
        cls.good = cls.root / "good"
        cls.missing = cls.root / "missing"
        cls.good.mkdir()
        cls.missing.mkdir()
        cc = os.environ.get("CC", "cc")
        for output, extra in ((cls.good, []), (cls.missing, ["-DMISSING_DEVICE_INFO"])):
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC",
                            "-shared", *extra, str(HERE / "fake_opencl.c"),
                            "-o", str(output / "libOpenCL.so")], check=True)
        cls.runner = cls.root / "harness"
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(SOURCE), str(HERE / "discovery_harness.c"),
                        "-pthread", "-ldl", "-Wl,--wrap=recv", "-Wl,--wrap=dlopen",
                        "-o", str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_harness(self, mode, scenario="success", missing=False):
        env = dict(os.environ, LD_LIBRARY_PATH=str(self.missing if missing else self.good),
                   RME_FAKE_CL_SCENARIO=scenario)
        return subprocess.run([str(self.runner), mode], env=env, text=True,
                              capture_output=True, timeout=10)

    def test_all_information_traverses_socket_and_ids_are_virtual(self):
        result = self.run_harness("full")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all discovery requests OK", result.stdout)

    def test_id_from_closed_session_is_rejected_in_new_session(self):
        result = self.run_harness("cross")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_clean_eof(self):
        result = self.run_harness("eof")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_truncated_frame_is_rejected(self):
        result = self.run_harness("short")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bad_magic_is_rejected(self):
        result = self.run_harness("badmagic")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unknown_operation_is_rejected(self):
        result = self.run_harness("badop")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_peer_disconnect_is_reported_without_sigpipe(self):
        result = self.run_harness("disconnected")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_platforms_status_has_no_payload(self):
        result = self.run_harness("no_platforms", "no_platforms")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_devices_status_has_no_payload(self):
        result = self.run_harness("no_devices", "no_gpu")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_client_rejects_malformed_responses(self):
        for mode in ("inject_oversize", "inject_short_header", "inject_short_payload", "inject_error_payload"):
            with self.subTest(mode=mode):
                result = self.run_harness(mode)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("failed", result.stdout.lower())

    def test_roundtrip_reports_missing_library(self):
        result = self.run_harness("inject_no_library")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("library unavailable", result.stdout.lower())

    def test_roundtrip_reports_reported_gpu(self):
        result = self.run_harness("roundtrip")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Fake GPU", result.stdout)
        self.assertIn("OpenCL 1.2", result.stdout)

    def test_roundtrip_does_not_report_cpu_as_gpu(self):
        result = self.run_harness("roundtrip", "cpu_only")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Fake CPU", result.stdout)
        self.assertNotIn("GPU", result.stdout)

    def test_roundtrip_prefers_gpu_after_cpu(self):
        result = self.run_harness("roundtrip", "cpu_then_gpu")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Fake GPU", result.stdout)
        self.assertNotIn("Fake CPU", result.stdout)

    def test_roundtrip_prefers_gpu_on_later_platform(self):
        result = self.run_harness("roundtrip", "cpu_platform_then_gpu")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Fake GPU", result.stdout)
        self.assertNotIn("Fake CPU", result.stdout)

    def test_roundtrip_rejects_truncated_result_buffer(self):
        result = self.run_harness("roundtrip_small")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_roundtrip_marks_truncated_device_name(self):
        result = self.run_harness("roundtrip", "long_device")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("truncated", result.stdout.lower())

    def test_rejects_changed_reported_platform_string_length(self):
        for scenario in ("platform_length_short", "platform_length_long"):
            with self.subTest(scenario=scenario):
                result = self.run_harness("platform_length", scenario)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_changed_reported_device_string_length(self):
        for scenario in ("device_length_short", "device_length_long"):
            with self.subTest(scenario=scenario):
                result = self.run_harness("device_length", scenario)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_incorrect_device_type_length(self):
        for scenario in ("type_length_short", "type_length_long"):
            with self.subTest(scenario=scenario):
                result = self.run_harness("type_length", scenario)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_incorrect_compiler_flag_length(self):
        for scenario in ("compiler_length_short", "compiler_length_long"):
            with self.subTest(scenario=scenario):
                result = self.run_harness("compiler_length", scenario)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_roundtrip_rejects_no_platforms(self):
        result = self.run_harness("roundtrip", "no_platforms")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertTrue(result.stdout.strip())

    def test_roundtrip_rejects_no_devices(self):
        result = self.run_harness("roundtrip", "no_gpu")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertTrue(result.stdout.strip())

    def test_roundtrip_rejects_missing_required_symbol(self):
        result = self.run_harness("roundtrip", missing=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("required symbol missing", result.stdout.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
