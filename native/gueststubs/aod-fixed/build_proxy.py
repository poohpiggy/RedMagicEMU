#!/usr/bin/env python3
"""Build the target Guest AOD proxy to a new explicit path; never install it."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


NDK_REVISION = "28.2.13676358"
ORIGINAL_PATH = "/system_ext/priv-app/SystemUI_MFV/SystemUI_MFV.apk!/lib/arm64-v8a/libzte_saliency.so"
MASK_PATH = "/system_ext/priv-app/SystemUI_MFV/lib/arm64/rme_aod_mask.raw"
SOURCE_DIR = Path(__file__).resolve().parent
SOURCE_NAMES = ("proxy.c", "sha256.c", "sha256.h")
EXPORTS = frozenset("Java_com_zte_saliency_SaliencyApi_" + suffix for suffix in (
    "saliencyMapInit", "saliencyInitStatus", "saliencyMapRunning",
    "saliencyIsAlphaNull", "saliencyMapHighestPointGet", "saliencyMapRelease",
))


def ndk_tools(ndk):
    properties = ndk / "source.properties"
    text = properties.read_text(encoding="utf-8")
    match = re.search(r"^Pkg\.Revision\s*=\s*(\S+)\s*$", text, re.MULTILINE)
    if not match or match.group(1) != NDK_REVISION:
        raise ValueError(f"NDK revision must be {NDK_REVISION}")
    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    extension = ".exe" if os.name == "nt" else ""
    bindir = ndk / "toolchains" / "llvm" / "prebuilt" / platform / "bin"
    compiler, readelf = (bindir / (name + extension) for name in ("clang", "llvm-readelf"))
    if not compiler.is_file() or not readelf.is_file():
        raise FileNotFoundError("NDK clang or llvm-readelf is missing")
    return compiler, readelf


def verify_elf(readelf, artifact):
    result = subprocess.run([str(readelf), "--wide", "--file-header", "--dynamic", "--dyn-syms", str(artifact)],
                            check=True, capture_output=True, text=True)
    text = result.stdout
    if not re.search(r"Class:\s+ELF64\b", text) or not re.search(r"Machine:\s+AArch64\b", text):
        raise ValueError("built library is not ARM64 ELF")
    soname = re.findall(r"Library soname:\s*\[([^]]+)\]", text)
    if soname != ["libzte_saliency.so"]:
        raise ValueError("unexpected library SONAME")
    needed = set(re.findall(r"Shared library:\s*\[([^]]+)\]", text))
    if needed != {"libc.so", "libdl.so"}:
        raise ValueError("unexpected direct shared-library dependencies")
    found = set(re.findall(r"\bFUNC\s+GLOBAL\s+DEFAULT\s+\S+\s+(Java_com_zte_saliency_SaliencyApi_\w+)\b", text))
    if found != EXPORTS:
        raise ValueError("unexpected JNI export set")
    data = artifact.read_bytes()
    if ((ORIGINAL_PATH.encode() + b"\0") not in data or (MASK_PATH.encode() + b"\0") not in data or
            b"RME_TEST_ORIGINAL_PATH" in data or b"/synthetic/original.so" in data or b"/synthetic/alpha.raw" in data):
        raise ValueError("incorrect fixed paths or experimental fixture in library")
    return hashlib.sha256(data).hexdigest()


def build(ndk, output):
    if output.exists() or output.is_symlink():
        raise FileExistsError("output already exists")
    if not output.parent.is_dir():
        raise FileNotFoundError("output directory does not exist")
    compiler, readelf = ndk_tools(ndk)
    sources = {name: (SOURCE_DIR / name).read_bytes() for name in SOURCE_NAMES}
    with tempfile.TemporaryDirectory(prefix="rme-aod-build-", dir=output.parent) as directory:
        work = Path(directory)
        for name, contents in sources.items():
            (work / name).write_bytes(contents)
        artifact = work / "libzte_saliency.so"
        command = [str(compiler), "--target=aarch64-linux-android28", "-std=c11", "-O2",
                   "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-pthread",
                   "-Wl,--no-undefined", "-Wl,--build-id=none", "-Wl,-soname,libzte_saliency.so",
                   f'-DRME_AOD_ORIGINAL_PATH="{ORIGINAL_PATH}"', f'-DRME_AOD_ALPHA_PATH="{MASK_PATH}"',
                   str(work / "proxy.c"), str(work / "sha256.c"), "-ldl", "-o", str(artifact)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        digest = verify_elf(readelf, artifact)
        # Atomic no-replace creation. The temporary file is on the output volume.
        os.link(artifact, output)
        return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        digest = build(args.ndk, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) and error.stderr else str(error)
        print(f"AOD proxy build failed: {detail}", file=sys.stderr)
        return 1
    print(f"AOD_PROXY_BUILD_OK sha256={digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
