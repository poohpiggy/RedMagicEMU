#!/usr/bin/env python3
"""Compare externally visible Vulkan names with the selected NDK loader."""
import subprocess
import sys


def names(nm, path, dynamic):
    flags = [nm, "--defined-only", "-g"] + (["-D"] if dynamic else [])
    result = subprocess.check_output(flags + [path], text=True)
    return {parts[-1] for line in result.splitlines() if (parts := line.split())
            and parts[-1].startswith("vk")}


if len(sys.argv) != 4:
    sys.exit("usage: check_ndk_exports.py LLVM_NM NDK_LIBVULKAN CLIENT_OBJECT")
reference = names(sys.argv[1], sys.argv[2], True)
actual = names(sys.argv[1], sys.argv[3], False)
missing = sorted(reference - actual)
print(f"NDK vk exports: {len(reference)}; missing from client: {len(missing)}")
if missing:
    print("\n".join(missing))
    sys.exit(1)
