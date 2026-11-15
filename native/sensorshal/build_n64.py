#!/usr/bin/env python3
"""Build the API-36 N64 sensors HAL with Android NDK 28.2.

The legacy ARMv7 common asset is intentionally not touched. The output is
compiled and inspected in a temporary directory before replacing the two N64
assets. Run the native tests first; see N64_DIAGNOSTICS.md for rollback.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_HOME"), required=not os.environ.get("ANDROID_NDK_HOME"))
    args = parser.parse_args()
    ndk = Path(args.ndk)
    properties = (ndk / "source.properties").read_text(encoding="utf-8")
    if "Pkg.Revision = 28.2.13676358" not in properties:
        raise SystemExit("expected Android NDK 28.2.13676358")
    host = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64"
    ext = ".exe" if sys.platform == "win32" else ""
    tools = ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin"
    clang, strip, readelf = (str(tools / (name + ext)) for name in ("clang", "llvm-strip", "llvm-readelf"))
    directory = Path(__file__).resolve().parent
    assets = directory.parents[1] / "app" / "src" / "main" / "assets" / "engines" / "n64-36"
    with tempfile.TemporaryDirectory(prefix="rme-sensors-hal-") as temporary:
        candidate = Path(temporary) / "sensors.default.so"
        subprocess.run([clang, "--target=aarch64-linux-android26", "-std=c11", "-D_GNU_SOURCE",
                        "-Wall", "-Wextra", "-Werror", "-Os", "-fPIC", "-shared", "-fvisibility=hidden",
                        "-Wl,-z,max-page-size=16384", "-Wl,--no-undefined", "-Wl,-soname,sensors.default.so",
                        "-o", str(candidate), str(directory / "n64_sensors_hal.c")], check=True)
        subprocess.run([strip, str(candidate)], check=True)
        description = subprocess.check_output([readelf, "-h", "-d", "-s", str(candidate)], text=True)
        for expected in ("ELF64", "AArch64", "sensors.default.so", " HMI"):
            if expected not in description:
                raise SystemExit("candidate missing expected ELF field: " + expected)
        for target in ("system#lib64#hw#sensors.default.so", "vendor#lib64#hw#sensors.default.so"):
            shutil.copyfile(candidate, assets / target)
            print("updated", assets / target)


if __name__ == "__main__":
    main()
