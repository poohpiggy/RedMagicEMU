#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise an isolated gfxstream build using a deliberately incomplete loader."""

import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
SUPERVISOR = HERE.parent / "gfxstream-memory-report" / "supervise.py"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def existing_file(raw):
    path = Path(raw)
    if not path.is_absolute() or not path.is_file():
        raise argparse.ArgumentTypeError(f"expected existing absolute file: {raw}")
    return path


def existing_dir(raw):
    path = Path(raw)
    if not path.is_absolute() or not path.is_dir():
        raise argparse.ArgumentTypeError(f"expected existing absolute directory: {raw}")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline-host", "patched-host", "headers"):
        parser.add_argument("--" + name, required=True, type=existing_dir)
    for name in ("server", "probe", "client-icd", "host-icd", "system-loader"):
        parser.add_argument("--" + name, required=True, type=existing_file)
    parser.add_argument("--dependency-dir", required=True, action="append", type=existing_dir)
    args = parser.parse_args()
    for host in (args.baseline_host, args.patched_host):
        require((host / "libgfxstream_backend.so.0").is_file(), f"missing backend in {host}")
    require((args.headers / "vulkan/vulkan.h").is_file(), "missing Vulkan headers")
    output = Path(tempfile.mkdtemp(prefix="rme-missing-properties2-", dir="/var/tmp"))
    print(f"artifacts={output}", flush=True)
    shim = output / "libvulkan-test.so"
    subprocess.run([
        "cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror", "-Wl,-Bsymbolic",
        "-I", str(args.headers), str(HERE / "shim.c"), "-Wl,--no-as-needed",
        str(args.system_loader), "-ldl", "-o", str(shim),
    ], check=True, timeout=30)
    inputs = [HERE / "shim.c", HERE / "run-case.sh", HERE / "missing-properties2-failure.patch",
              args.server, args.probe, args.client_icd, args.host_icd, args.system_loader,
              args.baseline_host / "libgfxstream_backend.so.0",
              args.patched_host / "libgfxstream_backend.so.0", shim]
    with (output / "input-sha256.txt").open("w") as f:
        for path in inputs:
            f.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path}\n")

    def run(label, mode, backend):
        case = output / label
        case.mkdir(mode=0o700)
        command = [sys.executable, str(SUPERVISOR), str(HERE / "run-case.sh"),
                   mode, str(case), str(backend), str(shim), str(args.server),
                   str(args.probe), str(args.client_icd), str(args.host_icd),
                   ":".join(map(str, args.dependency_dir))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=65)
        (case / "supervisor.log").write_text(result.stdout + result.stderr)
        print(f"{label}: supervisor={result.returncode} {result.stdout.strip()}", flush=True)
        server_log = (case / "server.log").read_text()
        client_path = case / "client.log"
        client_log = client_path.read_text() if client_path.is_file() else ""
        return result, server_log, client_log

    baseline, log, _ = run("baseline-both", "both", args.baseline_host)
    require(baseline.returncode == 3 and "status=139" in baseline.stdout,
            "baseline did not reproduce the expected SIGSEGV")
    for symbol in ("vkGetPhysicalDeviceProperties2", "vkGetPhysicalDeviceProperties2KHR"):
        require(f"TEST_SHIM_HIDDEN {symbol} instance=nonnull" in log,
                f"baseline did not hide {symbol}")
    require("claims to support ID properties" in log, "baseline failed before the target path")

    for mode in ("normal", "khr"):
        result, log, client = run("patched-" + mode, mode, args.patched_host)
        require(result.returncode == 0 and "client_status=0 server=alive" in result.stdout,
                f"patched {mode} case failed")
        require("server_final_status=143 forced_kill=0" in result.stdout,
                f"patched {mode} server cleanup failed")
        require("VULKAN_COMPUTE_OK words=64" in client and
                "Selecting Vulkan device: llvmpipe" in log,
                f"patched {mode} did not complete the CPU calculation")
        if mode == "khr":
            require("TEST_SHIM_HIDDEN vkGetPhysicalDeviceProperties2KHR instance=nonnull" in log,
                    "KHR alias was not hidden")

    result, log, _ = run("patched-both", "both", args.patched_host)
    require(result.returncode == 3 and "status=1" in result.stdout,
            "patched server did not reject initialization with exit 1")
    for message in ("TEST_SHIM_HIDDEN vkGetPhysicalDeviceProperties2 instance=nonnull",
                    "TEST_SHIM_HIDDEN vkGetPhysicalDeviceProperties2KHR instance=nonnull",
                    "Cannot initialize Vulkan: vkGetPhysicalDeviceProperties2",
                    "Failed to initialize FrameBuffer().", "Failed to initialize Gfxstream renderer!",
                    "RutabagaError(ComponentError(-22))"):
        require(message in log, f"missing failure evidence: {message}")
    created = re.findall(r"TEST_SHIM_CREATE result=0 instance=(0x[0-9a-f]+)", log)
    destroyed = re.findall(r"TEST_SHIM_DESTROY instance=(0x[0-9a-f]+)", log)
    require(len(created) == 1 and created == destroyed, "instance was not destroyed")
    require("Selecting Vulkan device:" not in log, "failure unexpectedly initialized a device")
    print("PASS: failure propagated and instance destroyed; positive tests use llvmpipe CPU only")
    print("LIMITATION: core-name-only absence has a separate uncorrected guest-instance failure")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
