#!/usr/bin/env python3
"""Compare the shipped API-36 ARM64 wrapper to a temporary reproducible build.

This uses the pinned NDK supplied by the caller and the existing AEMU_META build
configuration. It never writes to the product asset. A mismatch identifies a
stale or differently built artifact; it does not by itself prove runtime behavior.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, required=True)
    args = parser.parse_args()
    source_dir = Path(__file__).resolve().parent.parent
    repo = source_dir.parent.parent
    asset = repo / "app/src/main/assets/engines/n64-36/vendor#lib64#hw#gralloc.default.so"
    prebuilt = args.ndk / "toolchains/llvm/prebuilt"
    host = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64"
    ext = ".exe" if sys.platform == "win32" else ""
    tools = prebuilt / host / "bin"
    clang = tools / ("clang" + ext)
    strip = tools / ("llvm-strip" + ext)
    with tempfile.TemporaryDirectory(prefix="rme-gralloc-asset-") as temp:
        built = Path(temp) / "gralloc.default.so"
        command = [str(clang), "--target=aarch64-linux-android26", "-Os", "-fPIC", "-shared",
                   "-nostdlib", "-ffreestanding", "-fno-builtin", "-fvisibility=hidden",
                   "-fno-stack-protector", "-mno-outline-atomics", "-Wl,-z,norelro",
                   "-Wl,--build-id=none", "-Wl,--no-undefined", "-Wl,-soname,gralloc.default.so",
                   "-DAEMU_META", "-o", str(built), "gralloc_wrap.c", "-ldl", "-lc"]
        subprocess.run(command, cwd=source_dir, check=True)
        subprocess.run([str(strip), str(built)], check=True)
        old = asset.read_bytes()
        new = built.read_bytes()
        print("asset  SHA-256:", hashlib.sha256(old).hexdigest())
        print("source SHA-256:", hashlib.sha256(new).hexdigest())
        if old != new:
            print("FAIL: API-36 ARM64 AEMU_META asset differs from source build", file=sys.stderr)
            return 1
    print("GRALLOC_ASSET_REPRODUCIBLE_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
