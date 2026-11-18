#!/usr/bin/env python3
"""Exercise runner rejection paths with isolated fake executables (Linux/WSL)."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


RUNNER = Path(__file__).resolve().parent / "run.py"


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rme-clvk-runner-test-")
        self.path = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.lib = self.path / "lib"
        self.lib.mkdir()
        (self.lib / "libOpenCL.so.1").touch()
        self.compiler = self.path / "clspv"
        self.compiler.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        self.compiler.chmod(0o755)
        self.icd = self.path / "icd.json"
        self.icd.write_text("{}", encoding="utf-8")

    def run_case(self, mode, timeout="2"):
        executable = self.path / "simple_test"
        executable.write_text(
            "#!/usr/bin/env python3\n"
            "import os,sys,time\n"
            f"mode={mode!r}\n"
            "if mode == 'check_env' and ('OCL_ICD_VENDORS' in os.environ or 'OPENCL_VENDOR_PATH' in os.environ):\n"
            " print('inherited OpenCL override'); sys.exit(2)\n"
            "positive=os.environ.get('CLVK_CLSPV_PATH') == " + repr(str(self.compiler)) + "\n"
            "if mode == 'timeout': time.sleep(2)\n"
            "if positive:\n"
            " print('Platform: clvk\\nDevice: llvmpipe (test CPU)')\n"
            " if mode != 'missing_output': print('Buffer content verified, test passed.')\n"
            " sys.exit(1 if mode == 'positive_error' else 0)\n"
            "if mode == 'negative_success': sys.exit(0)\n"
            "if mode == 'negative_wrong_error': print('unrelated failure'); sys.exit(1)\n"
            "print('simple.cpp:76 error after CL call: -11'); sys.exit(1)\n",
            encoding="utf-8",
        )
        executable.chmod(0o755)
        env = os.environ.copy()
        if mode == "check_env":
            env.update({"OCL_ICD_VENDORS": "/bogus", "OPENCL_VENDOR_PATH": "/bogus"})
        return subprocess.run(
            [sys.executable, str(RUNNER), "--test", str(executable), "--library-dir", str(self.lib),
             "--clspv", str(self.compiler), "--icd", str(self.icd), "--timeout", timeout],
            capture_output=True, text=True, timeout=10, env=env,
        )

    def test_valid_control_and_result(self):
        result = self.run_case("ok")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("NEGATIVE_CONTROL_OK", result.stdout)
        self.assertIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_rejects_false_success_of_missing_compiler(self):
        result = self.run_case("negative_success")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing-compiler control did not fail as expected", result.stderr)
        self.assertNotIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_rejects_unrelated_negative_failure(self):
        result = self.run_case("negative_wrong_error")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing-compiler control did not fail as expected", result.stderr)
        self.assertNotIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_rejects_success_marker_with_failure_exit(self):
        result = self.run_case("positive_error")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("simple_test did not verify all results", result.stderr)
        self.assertNotIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_rejects_missing_result_verification(self):
        result = self.run_case("missing_output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("simple_test did not verify all results", result.stderr)
        self.assertNotIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_timeout_fails(self):
        result = self.run_case("timeout", "0.1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exceeded", result.stderr)
        self.assertNotIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_drops_inherited_opencl_overrides(self):
        result = self.run_case("check_env")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("OPENCL_VULKAN_PROBE_OK", result.stdout)

    def test_timeout_race_when_process_group_has_exited(self):
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("opencl_probe_runner", RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        process = mock.Mock(pid=123)
        process.communicate.side_effect = [subprocess.TimeoutExpired("test", 0.1), ("", "")]
        with mock.patch.object(module.subprocess, "Popen", return_value=process), \
             mock.patch.object(module.os, "killpg", side_effect=ProcessLookupError):
            with self.assertRaisesRegex(RuntimeError, "exceeded"):
                module.run_case("unused", {}, self.path, 0.1)
        self.assertEqual(process.communicate.call_count, 2)

    def test_rejects_missing_required_input(self):
        result = subprocess.run([sys.executable, str(RUNNER)], capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("required", result.stderr)


if __name__ == "__main__":
    unittest.main()
