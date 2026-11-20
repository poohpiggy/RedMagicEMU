"""Cross-compile the host decoder and the ARM64 Guest verification client."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    toolchains = args.ndk / "toolchains" / "llvm" / "prebuilt"
    compiler = "clang.exe" if sys.platform == "win32" else "clang"
    clang = next(iter(sorted(toolchains.glob(f"*/bin/{compiler}"))), None)
    if clang is None:
        parser.error(f"cannot find {compiler} under {toolchains}")

    source = Path(__file__).resolve().parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def build(api, name, files, libraries):
        subprocess.run(
            [
                str(clang), f"--target=aarch64-linux-android{api}",
                "-O2", "-Wall", "-Wextra", "-Werror",
                *(str(source / file) for file in files), *libraries,
                "-o", str(output / name),
            ],
            check=True,
        )

    build(29, "libaemuvideohost.so", ["host_decode.c"], ["-lmediandk", "-ldl"])
    build(26, "rme-video-probe", ["decode_probe.c", "video_client.c"], ["-lmediandk", "-lz"])


if __name__ == "__main__":
    main()
