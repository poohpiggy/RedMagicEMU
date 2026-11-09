#!/usr/bin/env python3
"""Build only the unwired vibration Binder library, preserving existing assets."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

NAME = "libaemuvibration.so"
SOURCES = (
    "vibration_binder_auth.c", "vibration_binder_handle.c", "vibration_binder_handle_jni.c",
    "vibration_publish.c", "vibration_publish_jni.c",
    "../guest64/36/vibration_bproto_client.c", "../guest64/36/vibration_bproto_lifecycle.c",
    "../guest64/36/vibration_service_registration.c", "../guest64/36/vibration_binder_parcel.c",
    "../guest64/36/vibration_binder_contract.c", "../guest64/36/vibration_binder_reply.c",
    "../guest64/36/vibration_bproto_step.c",
)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.name != NAME:
        parser.error(f"output must be named {NAME}")
    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    clang = args.ndk / "toolchains" / "llvm" / "prebuilt" / platform / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rme-vibration-lib-", dir=args.output.parent) as temp:
        built = Path(temp).resolve() / NAME
        subprocess.run([str(clang.resolve()), "--target=aarch64-linux-android29", "-std=c11", "-Os",
                        "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", "-pthread",
                        "-Wl,-soname," + NAME, "-o", str(built), *SOURCES],
                       cwd=Path(__file__).resolve().parent, check=True)
        data = built.read_bytes()
        try:
            os.link(built, args.output)
        except FileExistsError:
            if args.output.is_symlink() or not args.output.is_file() or args.output.read_bytes() != data:
                raise FileExistsError(f"Refusing to replace existing artifact: {args.output}")
        print("VIBRATION_LIBRARY_BUILD_OK sha256=" + hashlib.sha256(data).hexdigest())

if __name__ == "__main__":
    main()
