#!/usr/bin/env python3
"""Test actual gfxstream QSRI method bodies with inert local substitutes."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--cxx", required=True)
    ap.add_argument("--compiler-arg", action="append", default=[])
    ap.add_argument("--patch", type=Path)
    args = ap.parse_args()
    root_source = args.source.resolve(strict=True)
    original = (root_source / "guest/vulkan_enc/ResourceTracker.cpp").read_bytes()
    print("SOURCE_SHA256=" + hashlib.sha256(original).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="rme-qsri-fd-") as d:
        root = Path(d)
        candidate = root / "guest/vulkan_enc/ResourceTracker.cpp"
        candidate.parent.mkdir(parents=True)
        candidate.write_bytes(original)
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve(strict=True))],
                           cwd=root, check=True)
        text = candidate.read_text(encoding="utf-8")
        methods = []
        for name in ("exportSyncFdForQSRILocked", "on_vkQueueSignalReleaseImageANDROID"):
            pattern = rf"^VkResult ResourceTracker::{name}\([\s\S]*?^\}}"
            matches = re.findall(pattern, text, flags=re.MULTILINE)
            if len(matches) != 1:
                raise RuntimeError(f"expected exactly one complete {name} method")
            methods.append(matches[0])
        (root / "actual-functions.inc").write_text("\n\n".join(methods) + "\n", encoding="utf-8")
        here = Path(__file__).resolve().parent
        failures = []
        for macro in (0, 1):
            executable = root / f"test-{macro}"
            command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                       "-fno-exceptions", "-fno-rtti", "-ftrivial-auto-var-init=zero",
                       "-DVK_USE_PLATFORM_ANDROID_KHR=1", f"-DGFXSTREAM_ENABLE_GUEST_GOLDFISH={macro}",
                       *args.compiler_arg, "-I", str(root), str(here / "harness.cpp"), "-o", str(executable)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            if result.stdout:
                print(result.stdout, end="")
            if result.returncode:
                failures.append(f"macro={macro}: exit={result.returncode}\n{result.stderr}")
        if failures:
            print("FAILED:\n" + "\n".join(failures))
            return 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
