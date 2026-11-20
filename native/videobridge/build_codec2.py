"""Build the experimental NX809J Codec2 bundle against its media APEX."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ndk", "codec2", "headers", "guest-libraries", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--camera-encoder", action="store_true", help="also build the Guest camera's hardware AVC relay")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    output = args.output.resolve()
    host_dir = output / "jniLibs" / "arm64-v8a"
    guest_dir = output / "assets" / "rme-video"
    host_dir.mkdir(parents=True, exist_ok=True)
    guest_dir.mkdir(parents=True, exist_ok=True)

    compiler = "clang.exe" if sys.platform == "win32" else "clang"
    clang = next(iter(sorted((args.ndk / "toolchains/llvm/prebuilt").glob(f"*/bin/{compiler}"))), None)
    if clang is None:
        parser.error("Android NDK compiler not found")
    clangxx = clang.with_name("clang++.exe" if sys.platform == "win32" else "clang++")

    include_dirs = [
        *(args.codec2 / name for name in ("core/include", "components/base/include", "vndk/include")),
        *(args.headers / name for name in ("foundation", "utils", "libutils", "base", "cutils",
                                            "hardware", "nativewindow", "arect", "system", "log")),
        source,
    ]
    libs = ("libc++.so", "libcodec2_soft_common.so", "libcodec2_vndk.so", "libcodec2.so",
            "libutils.so", "libstagefright_foundation.so")

    def build_component(source_name, soname, destination, *extra):
        subprocess.run([
            str(clangxx), "--target=aarch64-linux-android29", "-std=c++20",
            "-O2", "-fPIC", "-shared", "-fno-rtti", "-mbranch-protection=standard",
            "-fvisibility=hidden", "-Wno-deprecated-enum-enum-conversion",
            "-Wno-deprecated-anon-enum-enum-conversion", "-D__ANDROID_VNDK__",
            *(f"-I{path}" for path in include_dirs), str(source / source_name),
            *map(str, extra), "-nostdlib++", f"-L{args.guest_libraries}", "-Wl,--no-undefined",
            "-Wl,--allow-shlib-undefined", "-Wl,-z,nodelete", f"-Wl,-soname,{soname}",
            *(f"-l:{library}" for library in libs), "-llog", "-ldl", "-o", str(destination),
        ], check=True)

    with tempfile.TemporaryDirectory(prefix="rme-video-build-") as temp:
        work = Path(temp)
        subprocess.run([sys.executable, str(source / "build.py"), "--ndk", str(args.ndk),
                        "--output", str(work)], check=True)
        client = work / "video_client.o"
        subprocess.run([str(clang), "--target=aarch64-linux-android29", "-O2", "-fPIC", "-c",
                        str(source / "video_client.c"), "-o", str(client)], check=True)

        for codec in ("avc", "hevc"):
            build_component("c2_decode.cpp", f"libcodec2_soft_{codec}dec.so", work / f"{codec}.so",
                            f"-DRME_HEVC={int(codec == 'hevc')}", client)

        if args.camera_encoder:
            build_component("c2_camera_encode.cpp", "libcodec2_soft_avcenc.so", work / "avcenc.so")
            subprocess.run([
                str(clang), "--target=aarch64-linux-android29", "-O2", "-Wall", "-Wextra", "-Werror",
                str(source / "host_camera_encode.c"), "-lmediandk", "-ldl",
                "-o", str(work / "libaemucameraencoder.so"),
            ], check=True)

        shutil.copyfile(work / "libaemuvideohost.so", host_dir / "libaemuvideohost.so")
        index = []
        for codec in ("avc", "hevc"):
            library = work / f"{codec}.so"
            shutil.copyfile(library, guest_dir / library.name)
            index.append(f"{codec}\t{hashlib.sha256(library.read_bytes()).hexdigest()}\n")
        (guest_dir / "guest.idx").write_text("".join(index), encoding="utf-8", newline="\n")
        if args.camera_encoder:
            library = work / "avcenc.so"
            shutil.copyfile(library, guest_dir / library.name)
            (guest_dir / "camera-encoder.sha256").write_text(hashlib.sha256(library.read_bytes()).hexdigest() + "\n",
                                                             encoding="ascii", newline="\n")
            shutil.copyfile(work / "libaemucameraencoder.so", host_dir / "libaemucameraencoder.so")


if __name__ == "__main__":
    main()
