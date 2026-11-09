#!/usr/bin/env python3
"""Build the isolated-name Android 16 engine without touching the standard engine."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


OUTPUT_NAME = "libaemu64_vibration_36.so"


def publish_built(built: Path, output: Path) -> None:
    if output.name != OUTPUT_NAME:
        raise ValueError(f"output must be named {OUTPUT_NAME}")
    try:
        os.link(built, output)
    except FileExistsError:
        if output.is_symlink() or not output.is_file() or output.read_bytes() != built.read_bytes():
            raise FileExistsError(f"Refusing to replace existing artifact: {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.name != OUTPUT_NAME:
        parser.error(f"--output must be named {OUTPUT_NAME}")

    source = Path(__file__).resolve().parent
    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    suffix = ".exe" if os.name == "nt" else ""
    tools = args.ndk / "toolchains" / "llvm" / "prebuilt" / platform / "bin"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rme-vibration-engine-", dir=args.output.parent) as temp:
        built = Path(temp) / OUTPUT_NAME
        subprocess.run([str(tools / ("clang" + suffix)), "--target=aarch64-linux-android26", "-O2",
                        "-static", "-nostdlib", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                        "-mno-outline-atomics", "-fno-pic", "-no-pie", "-Wall", "-Wno-unused-function",
                        "-Wl,--image-base=0x2000000000", "-Wl,--build-id=none", "-Wl,-z,norelro",
                        "-o", str(built), "start.S", "run64.c"], cwd=source, check=True)
        subprocess.run([str(tools / ("llvm-strip" + suffix)), str(built)], check=True)
        digest = hashlib.sha256(built.read_bytes()).hexdigest()
        publish_built(built, args.output)
    print(f"VIBRATION_ENGINE_BUILD_OK sha256={digest}")


if __name__ == "__main__":
    main()
