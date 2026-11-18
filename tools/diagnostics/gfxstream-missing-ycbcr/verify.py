#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reproduce gfxstream YCbCr dispatch failures with a test-only loader shim."""

import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIAGNOSTICS = HERE.parent
SUPERVISOR = DIAGNOSTICS / "gfxstream-memory-report" / "supervise.py"
RUN_CASE = DIAGNOSTICS / "gfxstream-missing-properties2" / "run-case.sh"


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
    for name in ("backend", "headers"):
        parser.add_argument("--" + name, required=True, type=existing_dir)
    for name in ("server", "probe", "client-icd", "host-icd", "system-loader"):
        parser.add_argument("--" + name, required=True, type=existing_file)
    parser.add_argument("--dependency-dir", required=True, action="append", type=existing_dir)
    args = parser.parse_args()
    require((args.backend / "libgfxstream_backend.so.0").is_file(), "missing backend")
    require((args.headers / "vulkan/vulkan.h").is_file(), "missing Vulkan headers")
    output = Path(tempfile.mkdtemp(prefix="rme-missing-ycbcr-", dir="/var/tmp"))
    print(f"artifacts={output}", flush=True)
    shim = output / "libvulkan-test.so"
    subprocess.run([
        "cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror", "-Wl,-Bsymbolic",
        "-I", str(args.headers), str(HERE / "shim.c"), "-Wl,--no-as-needed",
        str(args.system_loader), "-ldl", "-o", str(shim),
    ], check=True, timeout=30)
    inputs = [HERE / "shim.c", HERE / "verify.py", RUN_CASE, SUPERVISOR,
              args.backend / "libgfxstream_backend.so.0", args.server, args.probe,
              args.client_icd, args.host_icd, args.system_loader, shim]
    with (output / "input-sha256.txt").open("w") as f:
        for path in inputs:
            f.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path}\n")

    def run(mode):
        case = output / mode
        case.mkdir(mode=0o700)
        command = [sys.executable, str(SUPERVISOR), str(RUN_CASE), mode, str(case),
                   str(args.backend), str(shim), str(args.server), str(args.probe),
                   str(args.client_icd), str(args.host_icd),
                   ":".join(map(str, args.dependency_dir))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=65)
        (case / "supervisor.log").write_text(result.stdout + result.stderr)
        print(f"{mode}: supervisor={result.returncode} {result.stdout.strip()}", flush=True)
        server = (case / "server.log").read_text()
        client_path = case / "client.log"
        client = client_path.read_text() if client_path.is_file() else ""
        return result, server, client

    result, log, client = run("normal")
    require(result.returncode == 0 and "client_status=0 server=alive" in result.stdout,
            "normal calculation failed")
    require("server_final_status=143 forced_kill=0" in result.stdout, "cleanup failed")
    require(re.search(r"Selecting Vulkan device: llvmpipe .*Version: 1\.4\.", log) and
            "VULKAN_COMPUTE_OK words=64" in client,
            "normal case did not complete Vulkan 1.4 llvmpipe CPU calculation")
    for symbol in ("vkCreateSamplerYcbcrConversion", "vkDestroySamplerYcbcrConversion"):
        require(f"YCBCR_QUERY name={symbol} real_available=yes" in log,
                f"normal case did not resolve {symbol}")

    for mode in ("core", "both"):
        result, log, _ = run(mode)
        require(result.returncode == 3 and "server_exited_before_socket status=139" in result.stdout,
                f"{mode} did not reproduce the expected crash")
        require("YCBCR_CREATE_DEVICE result=0" in log, "logical device not created")
        require(re.search(r"Selecting Vulkan device: llvmpipe .*Version: 1\.4\.", log),
                "failure did not use Vulkan 1.4 llvmpipe")
        require("YCBCR_DEVICE_EXTENSION VK_KHR_sampler_ycbcr_conversion" in log,
                "KHR device extension was not enabled")
        for symbol in ("vkCreateSamplerYcbcrConversion", "vkDestroySamplerYcbcrConversion"):
            require(f"YCBCR_HIDDEN {symbol}" in log, f"core function not hidden: {symbol}")
            alias = symbol + "KHR"
            expected = (f"YCBCR_QUERY name={alias} real_available=yes" if mode == "core"
                        else f"YCBCR_HIDDEN {alias}")
            require(expected in log, f"missing alias evidence: {expected}")
    print("PASS: baseline crashes reproduced by synthetic fault injection; no fix is provided")
    print("LIMITATION: llvmpipe CPU Vulkan 1.4; real Vulkan 1.0+KHR and Android are unverified")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
