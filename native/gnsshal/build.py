#!/usr/bin/env python3
"""Build the ARM64 Guest GNSS service against the frozen Android GNSS AIDL API."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import subprocess
import tempfile


API_HASH = "3552d3a7d21e743a59aa62eb395be44dddc0797e"
SOURCE = Path(__file__).resolve().parent


def run(*argv):
    subprocess.run(argv, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("aidl", "ndk", "aidl-api", "binder-headers", "platform-binder", "output"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    args = parser.parse_args()
    if (args.aidl_api / ".hash").read_text(encoding="ascii").strip() != API_HASH:
        parser.error("expected Android GNSS AIDL V4")

    windows = os.name == "nt"
    tools = args.ndk / "toolchains/llvm/prebuilt" / ("windows-x86_64" if windows else "linux-x86_64") / "bin"
    ext = ".exe" if windows else ""
    clang, clangxx, strip = (tools / (name + ext) for name in ("clang", "clang++", "llvm-strip"))
    target = "--target=aarch64-linux-android34"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rme-gnss-", dir=args.output.parent) as tmp:
        work = Path(tmp)
        include, generated = work / "include", work / "generated"
        for path in sorted(args.aidl_api.rglob("*.aidl")):
            run(args.aidl, "--lang=ndk", "--structured", "--stability=vintf", "-v", "4",
                f"--hash={API_HASH}", f"-I{args.aidl_api}", f"-o{generated}", f"-h{include}", path)

        host_fix = work / "host_fix.o"
        run(clang, target, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-c", SOURCE / "host_fix.c", "-o", host_fix)

        def compile_cpp(path):
            obj = work / (path.stem + ".o")
            flags = ("-Wall", "-Wextra", "-Werror") if path.parent == SOURCE else ()
            run(clangxx, target, "-std=c++20", "-O2", "-fvisibility=hidden", *flags,
                "-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__", "-DBINDER_STABILITY_SUPPORT",
                "-I", include, "-I", args.binder_headers, "-c", path, "-o", obj)
            return obj

        sources = sorted(generated.rglob("*.cpp")) + [SOURCE / "service.cpp"]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            objects = list(pool.map(compile_cpp, sources))
        executable = work / "aemu-gnss-service"
        run(clangxx, target, "-static-libstdc++", "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384",
            *objects, host_fix, args.platform_binder, "-o", executable)
        run(strip, executable)
        os.replace(executable, args.output)
    print(f"Built {args.output}")


if __name__ == "__main__":
    main()
