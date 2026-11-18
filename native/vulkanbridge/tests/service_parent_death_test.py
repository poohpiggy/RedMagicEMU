#!/usr/bin/env python3
"""Linux process-lifetime tests using a fake Vulkan loader; no GPU or Guest."""
import os
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = os.environ["RME_VK_SERVICE_TEST_BINARY"]


def launcher():
    env = os.environ.copy()
    env["RME_VK_FAKE_CASE"] = "hang_instance"
    child = subprocess.Popen([BINARY, "--parent-pid", str(os.getpid())],
                             stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE, env=env)
    ready, _, _ = select.select([child.stderr], [], [], 5)
    if not ready or child.stderr.readline() != b"fake:instance-create\n":
        child.kill()
        child.wait()
        raise RuntimeError("fake instance did not start")
    print(child.pid, flush=True)
    signal.pause()


def preexec_launcher(log_path):
    parent_pid = os.getpid()
    child_pid = os.fork()
    if child_pid == 0:
        with open(log_path, "wb", buffering=0) as log:
            os.dup2(log.fileno(), 2)
        os.kill(os.getpid(), signal.SIGSTOP)
        os.execv(BINARY, [BINARY, "--parent-pid", str(parent_pid)])
    _, status = os.waitpid(child_pid, os.WUNTRACED)
    if not os.WIFSTOPPED(status):
        raise RuntimeError("child did not stop before exec")
    print(child_pid, flush=True)
    signal.pause()


def is_live(pid):
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii") as proc:
            stat = proc.read()
        return stat[stat.rfind(")") + 2] != "Z"
    except FileNotFoundError:
        return False


class ParentDeathTest(unittest.TestCase):
    def test_death_during_hung_instance_kills_child(self):
        parent = subprocess.Popen([sys.executable, __file__, "launcher"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child_pid = None
        try:
            ready, _, _ = select.select([parent.stdout], [], [], 5)
            self.assertTrue(ready, "launcher did not report a child")
            child_pid = int(parent.stdout.readline())
            self.assertTrue(is_live(child_pid))
            parent.kill()
            parent.wait(timeout=5)
            deadline = time.monotonic() + 3
            while is_live(child_pid) and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertFalse(is_live(child_pid), "orphaned child survived parent death")
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait()
            if child_pid is not None and is_live(child_pid):
                os.kill(child_pid, signal.SIGKILL)
            parent.stdout.close()
            parent.stderr.close()

    def test_wrong_parent_is_rejected_before_instance_creation(self):
        result = subprocess.run([BINARY, "--parent-pid", "1"], input=b"Q",
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertNotIn(b"fake:instance-create", result.stderr)

    def test_parent_dies_before_child_exec(self):
        with tempfile.TemporaryDirectory() as temp:
            log = os.path.join(temp, "stderr")
            parent = subprocess.Popen([sys.executable, __file__, "preexec", log],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            child_pid = None
            try:
                ready, _, _ = select.select([parent.stdout], [], [], 5)
                self.assertTrue(ready, "launcher did not report a child")
                child_pid = int(parent.stdout.readline())
                parent.kill()
                parent.wait(timeout=5)
                os.kill(child_pid, signal.SIGCONT)
                deadline = time.monotonic() + 3
                while is_live(child_pid) and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertFalse(is_live(child_pid), "reparented child survived")
                with open(log, "rb") as f:
                    error = f.read()
                self.assertIn(b"RME_VK_HOST_PARENT_FAILED", error)
                self.assertNotIn(b"fake:instance-create", error)
            finally:
                if parent.poll() is None:
                    parent.kill()
                    parent.wait()
                if child_pid is not None and is_live(child_pid):
                    os.kill(child_pid, signal.SIGKILL)
                parent.stdout.close()
                parent.stderr.close()

    def test_missing_or_malformed_parent_is_rejected(self):
        for args in ([], ["--parent-pid", "0"], ["--parent-pid", "-1"],
                     ["--parent-pid", "42x"], ["--parent-pid", "999999999999999999999"]):
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], input=b"Q", stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn(b"fake:instance-create", result.stderr)


if __name__ == "__main__":
    if sys.argv[1:] == ["launcher"]:
        launcher()
    elif len(sys.argv) == 3 and sys.argv[1] == "preexec":
        preexec_launcher(sys.argv[2])
    else:
        unittest.main()
