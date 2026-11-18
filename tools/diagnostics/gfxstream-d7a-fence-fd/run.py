#!/usr/bin/env python3
"""Exercise the actual d7a method with local substitutes; no real FDs are used."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--compiler-arg", action="append", default=[])
    parser.add_argument("--patch", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    source_root = args.source.resolve(strict=True)
    source = source_root / "guest/vulkan_enc/ResourceTracker.cpp"
    vulkan_header = source_root / "guest/mesa/include/vulkan/vulkan_core.h"
    match = re.search(r"\bVK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT\s*=\s*(0x[0-9a-fA-F]+)",
                      vulkan_header.read_text(encoding="utf-8"))
    if not match or int(match.group(1), 16) != 0x8:
        raise RuntimeError("harness SYNC_FD flag does not match the source Vulkan header")
    original = source.read_bytes()
    print("SOURCE_SHA256=" + hashlib.sha256(original).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="rme-fence-fd-") as d:
        root = Path(d)
        candidate = root / "guest/vulkan_enc/ResourceTracker.cpp"
        candidate.parent.mkdir(parents=True)
        candidate.write_bytes(original)
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve(strict=True))],
                           cwd=root, check=True)
        text = candidate.read_text(encoding="utf-8")
        pattern = r"^VkResult ResourceTracker::on_vkGetFenceFdKHR\([\s\S]*?^\}"
        matches = re.findall(pattern, text, flags=re.MULTILINE)
        if len(matches) != 1:
            raise RuntimeError("expected exactly one complete on_vkGetFenceFdKHR method")
        (root / "actual-function.inc").write_text(matches[0] + "\n", encoding="utf-8")
        errors = []
        for macro in (0, 1):
            executable = root / f"test-{macro}"
            cmd = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0", "-fno-exceptions", "-fno-rtti",
                   f"-DGFXSTREAM_ENABLE_GUEST_GOLDFISH={macro}", *args.compiler_arg,
                   "-I", str(root), str(here / "harness.cpp"), "-o", str(executable)]
            subprocess.run(cmd, check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            if result.stdout:
                print(result.stdout, end="")
            if result.returncode:
                errors.append(f"macro={macro}: exit={result.returncode}\n{result.stderr}")
        if errors:
            print("FAILED:\n" + "\n".join(errors))
            return 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
