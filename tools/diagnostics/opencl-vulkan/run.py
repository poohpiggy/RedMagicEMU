#!/usr/bin/env python3
"""Run an existing clvk simple_test on a selected Linux Vulkan ICD."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile


SUCCESS = "Buffer content verified, test passed."


def run_case(executable, env, directory, timeout):
    process = subprocess.Popen([str(executable)], cwd=directory, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace", start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The process group already exited after the timeout.
        process.communicate()
        raise RuntimeError(f"simple_test exceeded {timeout:g} seconds") from None
    return process.returncode, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", required=True, type=Path, help="clvk simple_test executable")
    parser.add_argument("--library-dir", required=True, type=Path, help="directory containing libOpenCL.so.1")
    parser.add_argument("--clspv", required=True, type=Path, help="matching clspv executable")
    parser.add_argument("--icd", required=True, type=Path, help="Vulkan ICD JSON")
    parser.add_argument("--timeout", type=float, default=60, help="seconds per run, >0 and <=600")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("run this diagnostic on Linux or WSL")
    if not 0 < args.timeout <= 600:
        parser.error("timeout must be greater than zero and at most 600 seconds")

    executable, library, compiler, icd = (p.resolve() for p in
                                         (args.test, args.library_dir, args.clspv, args.icd))
    for path in (executable, compiler):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"executable not found or not executable: {path}")
    if not (library / "libOpenCL.so.1").is_file():
        parser.error(f"libOpenCL.so.1 not found in: {library}")
    if not icd.is_file():
        parser.error(f"Vulkan ICD JSON not found: {icd}")

    with tempfile.TemporaryDirectory(prefix="rme-opencl-vulkan-") as work:
        # Keep experiment state under one fresh directory. Do not inherit Vulkan,
        # OpenCL or loader overrides that could select a different implementation.
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(("CLVK_", "OCL_", "OPENCL_", "VK_", "MESA_", "LD_"))}
        env.update({"LD_LIBRARY_PATH": str(library), "VK_DRIVER_FILES": str(icd),
                    "VK_ICD_FILENAMES": str(icd), "CLVK_CACHE_DIR": "",
                    "CLVK_COMPILER_TEMP_DIR": work, "TMPDIR": work,
                    "XDG_CACHE_HOME": work, "MESA_SHADER_CACHE_DISABLE": "true"})

        env["CLVK_CLSPV_PATH"] = str(Path(work) / "compiler-intentionally-missing")
        code, output = run_case(executable, env, work, args.timeout)
        if code != 1 or "error after CL call: -11" not in output or SUCCESS in output:
            raise RuntimeError(f"missing-compiler control did not fail as expected (exit {code}):\n{output}")
        print("NEGATIVE_CONTROL_OK: missing compiler returned CL_BUILD_PROGRAM_FAILURE")

        env["CLVK_CLSPV_PATH"] = str(compiler)
        code, output = run_case(executable, env, work, args.timeout)
        print(output, end="" if output.endswith("\n") else "\n")
        if (code != 0 or SUCCESS not in output or "Platform: clvk" not in output or
                not re.search(r"(?m)^Device: \S", output) or "Failed comparison" in output):
            raise RuntimeError(f"simple_test did not verify all results (exit {code})")
        print("OPENCL_VULKAN_PROBE_OK: standalone runtime; device type and EMU integration require separate verification")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
