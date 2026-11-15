#!/usr/bin/env python3
"""Rebuild the target-only multi-rate composer with Android NDK 28.2."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_HOME"),
                        required=not os.environ.get("ANDROID_NDK_HOME"))
    args = parser.parse_args()
    ndk = Path(args.ndk)
    if "Pkg.Revision = 28.2.13676358" not in (ndk / "source.properties").read_text(encoding="utf-8"):
        raise SystemExit("expected Android NDK 28.2.13676358")
    host = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64"
    ext = ".exe" if sys.platform == "win32" else ""
    tools = ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin"
    clang, strip, readelf = (str(tools / (name + ext)) for name in ("clang", "llvm-strip", "llvm-readelf"))
    source = Path(__file__).resolve().parent
    asset = source.parents[1] / "app/src/main/assets/engines/redmagic/hwcomposer-refresh.so"
    with tempfile.TemporaryDirectory(prefix="rme-refresh-hwc-") as directory:
        temp = Path(directory)
        stub = temp / "hw.c"
        stub.write_text("int hw_get_module(const char *id, const void **m) { (void)id; (void)m; return -1; }\n", encoding="utf-8")
        subprocess.run([clang, "--target=aarch64-linux-android26", "-shared", "-nostdlib",
                        "-Wl,-soname,libhardware.so", "-o", str(temp / "libhardware.so"), str(stub)], check=True)
        candidate = temp / "hwcomposer.default.so"
        subprocess.run([clang, "--target=aarch64-linux-android26", "-O2", "-fPIC", "-shared", "-nostdlib",
                        "-fno-builtin", "-fvisibility=hidden", "-fno-stack-protector",
                        "-DAEMU_NX809J_DYNAMIC_REFRESH", "-Wl,-z,norelro", "-Wl,--build-id=none",
                        "-Wl,--no-undefined", "-Wl,-soname,hwcomposer.default.so", "-o", str(candidate),
                        str(source / "hwc.c"), "-L" + str(temp), "-lhardware", "-lc"], check=True)
        subprocess.run([strip, str(candidate)], check=True)
        description = subprocess.check_output([readelf, "-h", "-d", "-s", str(candidate)], text=True)
        for expected in ("ELF64", "AArch64", "hwcomposer.default.so", " HMI", "libhardware.so"):
            if expected not in description:
                raise SystemExit("candidate missing expected ELF field: " + expected)
        shutil.copyfile(candidate, asset)
        print("updated", asset)


if __name__ == "__main__":
    main()
