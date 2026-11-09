#!/usr/bin/env python3
"""Build the ARM64 Guest adapter without replacing an existing different artifact."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    compiler = args.ndk / "toolchains" / "llvm" / "prebuilt" / platform / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    sources = ["qti_autoload.c", "qti_session.c", "qti_led_shim.c", "qti_led_adapter.c", "guest_sender.c"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rme-vibration-build-", dir=args.output.parent) as temp:
        built = Path(temp) / "adapter.so"
        subprocess.run([str(compiler), "--target=aarch64-linux-android29", "-std=gnu11", "-O2",
                        "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-pthread",
                        "-Wl,--build-id=none", *[str(source / name) for name in sources], "-o", str(built)], check=True)
        try:
            os.link(built, args.output)
        except FileExistsError:
            if args.output.is_symlink() or not args.output.is_file() or args.output.read_bytes() != built.read_bytes():
                raise FileExistsError(f"Refusing to overwrite existing artifact: {args.output}")
    print("VIBRATION_ADAPTER_BUILD_OK")


if __name__ == "__main__":
    main()
