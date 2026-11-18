"""Exercise the host probe with a process-local fake OpenCL implementation.

Run on Linux/WSL: python3 native/opencl-host-probe/tests/test_probe.py
The test never loads the system OpenCL library or accesses a GPU.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "probe.c"


class FakeOpenClTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="rme-opencl-test-")
        cls.root = Path(cls.temp.name)
        cls.good = cls.root / "good"
        cls.missing = cls.root / "missing"
        cls.good.mkdir()
        cls.missing.mkdir()
        cls.runner = cls.root / "runner"
        cc = os.environ.get("CC", "cc")
        for output, extra in ((cls.good, []), (cls.missing, ["-DMISSING_BUILD"])):
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC",
                            "-shared", *extra, str(HERE / "fake_opencl.c"),
                            "-o", str(output / "libOpenCL.so")], check=True)
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(SOURCE),
                        str(HERE / "harness.c"), "-ldl", "-o", str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_probe(self, scenario, missing=False):
        env = dict(os.environ, LD_LIBRARY_PATH=str(self.missing if missing else self.good),
                   RME_FAKE_CL_SCENARIO=scenario)
        return subprocess.run([str(self.runner)], env=env, text=True,
                              capture_output=True, timeout=10)

    def test_executes_and_checks_64_values(self):
        result = self.run_probe("success")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("GPU", result.stdout)
        self.assertIn("64", result.stdout)

    def test_reports_no_gpu(self):
        result = self.run_probe("no_gpu")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("clGetDeviceIDs", result.stdout)

    def test_selects_gpu_platform_when_creating_context(self):
        result = self.run_probe("multi_platform")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("64", result.stdout)

    def test_reports_program_build_failure(self):
        result = self.run_probe("build_fail")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("clBuildProgram", result.stdout)

    def test_reports_missing_compiler(self):
        result = self.run_probe("no_compiler")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("compiler", result.stdout.lower())

    def test_reports_read_failure(self):
        result = self.run_probe("read_fail")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("clEnqueueReadBuffer", result.stdout)

    def test_reports_cleanup_failure_after_success(self):
        result = self.run_probe("release_fail")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("clReleaseMemObject", result.stdout)

    def test_rejects_incorrect_computation(self):
        result = self.run_probe("bad_result")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("mismatch", result.stdout.lower())

    def test_reports_missing_required_symbol(self):
        result = self.run_probe("success", missing=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("clBuildProgram", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
