#!/usr/bin/env python3
"""Exercise the real service process with a fake Vulkan loader, never a GPU."""
import os
from pathlib import Path
import select
import struct
import subprocess
import unittest


class ServiceProcessTest(unittest.TestCase):
    def setUp(self):
        self.binary = Path(os.environ["RME_VK_SERVICE_TEST_BINARY"])
        self.assertTrue(self.binary.is_file(), "service test executable has not been built")

    def launch(self, scenario="ok"):
        env = os.environ.copy()
        env["RME_VK_FAKE_CASE"] = scenario
        return subprocess.Popen([str(self.binary), "--parent-pid", str(os.getpid())], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

    def test_ready_then_explicit_stop(self):
        p = self.launch()
        try:
            ready, _, _ = select.select([p.stdout], [], [], 5)
            self.assertTrue(ready, "service never announced readiness")
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            self.assertIsNone(p.poll(), "service exited before stop")
            out, err = p.communicate(b"Q", timeout=5)
            self.assertEqual(p.returncode, 0, err)
            self.assertEqual(out, b"")
            self.assertEqual(err, b"fake:instance-create\nfake:device-create\nRME_VK_DEVICE name=fake?GPU type=1 api=1.2.0\nfake:device-destroy\nfake:instance-destroy\n")
        finally:
            if p.poll() is None: p.kill(); p.wait()

    def test_ready_then_eof_stops(self):
        p = self.launch()
        out, err = p.communicate(b"", timeout=5)
        self.assertEqual(p.returncode, 0, err)
        self.assertEqual(out, b"RME_VK_HOST_READY\n")
        self.assertEqual(err, b"fake:instance-create\nfake:device-create\nRME_VK_DEVICE name=fake?GPU type=1 api=1.2.0\nfake:device-destroy\nfake:instance-destroy\n")

    def test_unknown_control_fails_and_closes_device(self):
        p = self.launch()
        out, err = p.communicate(b"X", timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"RME_VK_HOST_READY\n")
        self.assertIn(b"RME_VK_HOST_INVALID_CONTROL", err)
        self.assertIn(b"fake:device-destroy\nfake:instance-destroy\n", err)

    def test_instance_failure_is_not_ready(self):
        p = self.launch("instance_fail")
        out, err = p.communicate(b"", timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"")
        self.assertIn(b"RME_VK_HOST_INSTANCE_FAILED", err)
        self.assertNotIn(b"fake:device-create", err)
        self.assertNotIn(b"fake:instance-destroy", err)

    def test_device_failure_is_not_ready(self):
        p = self.launch("device_fail")
        out, err = p.communicate(b"", timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"")
        self.assertIn(b"RME_VK_HOST_DEVICE_FAILED", err)
        self.assertIn(b"fake:instance-destroy", err)
        self.assertNotIn(b"fake:device-destroy", err)

    def test_cpu_device_is_rejected(self):
        p = self.launch("cpu")
        out, err = p.communicate(b"", timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"")
        self.assertIn(b"RME_VK_HOST_DEVICE_FAILED", err)
        self.assertIn(b"fake:instance-destroy", err)
        self.assertNotIn(b"fake:device-create", err)

    def test_fixed_command_fragmented_and_quit_at_boundary(self):
        p = self.launch()
        try:
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            values = list(range(64))
            values[7] = 0x51005151  # Includes literal 'Q' bytes inside the payload.
            frame = b"C" + struct.pack("<64I", *values)
            for part in (frame[:1], frame[1:3], frame[3:111], frame[111:]):
                p.stdin.write(part); p.stdin.flush()
            ready, _, _ = select.select([p.stdout], [], [], 5)
            self.assertTrue(ready, "fake compute reply missing")
            self.assertEqual(p.stdout.read(4), b"RMC1")
            result = struct.unpack("<64I", p.stdout.read(256))
            self.assertEqual(result, tuple((v * 3 + 7) & 0xffffffff for v in values))
            out, err = p.communicate(b"Q", timeout=5)
            self.assertEqual(p.returncode, 0, err)
            self.assertEqual(out, b"")
        finally:
            if p.poll() is None: p.kill(); p.wait()

    def test_two_fixed_commands_preserve_order(self):
        p = self.launch()
        try:
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            for offset in (0, 123):
                values = [(i + offset) & 0xffffffff for i in range(64)]
                p.stdin.write(b"C" + struct.pack("<64I", *values)); p.stdin.flush()
                self.assertEqual(p.stdout.read(4), b"RMC1")
                self.assertEqual(struct.unpack("<64I", p.stdout.read(256)),
                                 tuple((v * 3 + 7) & 0xffffffff for v in values))
            _, err = p.communicate(b"Q", timeout=5)
            self.assertEqual(p.returncode, 0, err)
        finally:
            if p.poll() is None: p.kill(); p.wait()

    def test_truncated_fixed_command_is_failure(self):
        p = self.launch()
        out, err = p.communicate(b"C\x01\x02", timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"RME_VK_HOST_READY\n")
        self.assertIn(b"RME_VK_HOST_REQUEST_FAILED", err)

    def test_partial_fixed_command_times_out_even_if_pipe_open(self):
        p = self.launch()
        try:
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            p.stdin.write(b"C\x01\x02"); p.stdin.flush()
            self.assertNotEqual(p.wait(timeout=5), 0)
            self.assertEqual(p.stdout.read(), b"")
            self.assertIn(b"RME_VK_HOST_REQUEST_FAILED", p.stderr.read())
        finally:
            if p.poll() is None: p.kill(); p.wait()
            p.stdin.close(); p.stdout.close(); p.stderr.close()

    def test_closed_response_pipe_is_failure(self):
        p = self.launch()
        try:
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            p.stdout.close()
            p.stdin.write(b"C" + bytes(256)); p.stdin.flush()
            self.assertNotEqual(p.wait(timeout=5), 0)
            self.assertIn(b"RME_VK_HOST_RESPONSE_FAILED", p.stderr.read())
        finally:
            if p.poll() is None: p.kill(); p.wait()
            p.stdin.close(); p.stderr.close()

    def test_fixed_failure_never_returns_success_reply(self):
        p = self.launch("compute_fail")
        out, err = p.communicate(b"C" + bytes(256), timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"RME_VK_HOST_READY\n")
        self.assertIn(b"RME_VK_HOST_COMPUTE_FAILED", err)

    def test_unresolved_compute_exits_without_device_teardown(self):
        p = self.launch("compute_unresolved")
        out, err = p.communicate(b"C" + bytes(256), timeout=5)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(out, b"RME_VK_HOST_READY\n")
        self.assertIn(b"RME_VK_HOST_COMPUTE_UNRESOLVED", err)
        self.assertNotIn(b"fake:device-destroy", err)
        self.assertNotIn(b"fake:instance-destroy", err)

    def test_unresolved_compute_exits_when_stderr_is_full(self):
        p = self.launch("compute_unresolved_stderr_full")
        try:
            self.assertEqual(p.stdout.readline(), b"RME_VK_HOST_READY\n")
            p.stdin.write(b"C" + bytes(256)); p.stdin.flush()
            self.assertNotEqual(p.wait(timeout=3), 0)
            self.assertEqual(p.stdout.read(), b"")
            self.assertNotIn(b"fake:device-destroy", p.stderr.read())
        finally:
            if p.poll() is None: p.kill(); p.wait()
            p.stdin.close(); p.stdout.close(); p.stderr.close()


if __name__ == "__main__": unittest.main()
