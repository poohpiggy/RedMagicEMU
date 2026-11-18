#!/usr/bin/env python3
"""Compile the original d7a33801 objects with and without the Android macro."""
import argparse
import ast
import configparser
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
HEADER = "include/render-utils/render_api_platform_types.h"
HEADER_HASH = "f9d3a43f62426963e3c28cda16d55cfaa23f0dcd0116da3f2a9aab0eee6661a6"
CROSS_HASH = "a3cd21c2ff9f3e374dfde582c5cee3c7b3138be3010deb66ac6db9b09b63365e"
TARGETS = ("DisplaySurfaceGl.cpp", "EmulationGl.cpp", "EglOsApi_egl.cpp")
BASELINE_MARKERS = {
    "DisplaySurfaceGl.cpp": ("error:", "FBNativeWindowType", "EGLNativeWindowType", "ANativeWindow *"),
    "EmulationGl.cpp": ("error:", "FBNativeWindowType", "EGLNativeWindowType", "ANativeWindow *"),
    "EglOsApi_egl.cpp": ("error:", "Drawable", "EGLNativeWindowType", "ANativeWindow *"),
}


def compile_one(entry, output, extra=()):
    command = shlex.split(entry["command"])
    for flag in ("-o", "-MF", "-MQ"):
        i = command.index(flag)
        command[i + 1] = str(output) + (".d" if flag == "-MF" else "")
    command[1:1] = list(extra)
    return subprocess.run(command, cwd=entry["directory"], capture_output=True, text=True, timeout=90)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--android-build", required=True, type=Path)
    parser.add_argument("--linux-build", required=True, type=Path)
    parser.add_argument("--cross", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    cross = args.cross.resolve()
    if hashlib.sha256((source / HEADER).read_bytes()).hexdigest() != HEADER_HASH:
        raise RuntimeError("fixed source header does not match")
    if hashlib.sha256(cross.read_bytes()).hexdigest() != CROSS_HASH:
        raise RuntimeError("original cross file does not match")
    entries = json.loads((args.android_build.resolve() / "compile_commands.json").read_text())
    with tempfile.TemporaryDirectory(prefix="rme-android-platform-") as directory:
        root = Path(directory)
        for name in TARGETS:
            matches = [x for x in entries if x["file"].endswith("/" + name)]
            if len(matches) != 1:
                raise RuntimeError("compile command is missing or ambiguous: " + name)
            result = compile_one(matches[0], root / (name + ".old.o"))
            if result.returncode == 0 or not all(marker in result.stderr for marker in BASELINE_MARKERS[name]):
                raise RuntimeError("baseline did not show the expected window type mismatch: " + name + "\n" + result.stderr[-2000:])
        print("ANDROID_WINDOW_BASELINES_FAILED", flush=True)
        patch = HERE / "cross-android-define.patch"
        if not patch.is_file():
            raise AssertionError("Android cross-file patch is missing")
        (root / "cross.ini").write_bytes(cross.read_bytes())
        subprocess.run(["patch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=root, check=True, capture_output=True, text=True)
        config = configparser.ConfigParser()
        config.read(root / "cross.ini")
        cpp_args = ast.literal_eval(config["built-in options"]["cpp_args"])
        c_args = ast.literal_eval(config["built-in options"]["c_args"])
        if cpp_args != ["-DANDROID"] or c_args != ["-DANDROID"]:
            raise AssertionError("cross file must define ANDROID for both languages")
        for name in TARGETS:
            entry = next(x for x in entries if x["file"].endswith("/" + name))
            built = compile_one(entry, root / (name + ".new.o"), cpp_args)
            if built.returncode:
                raise RuntimeError(name + " still fails: " + built.stderr[-3000:])
            print("ANDROID_OBJECT_OK " + name, flush=True)
        # Only Android's cross-file options change. Check the unmodified desktop
        # Linux header branch directly, without running or loading GLES.
        test = root / "linux-type.cpp"
        test.write_text('#include <type_traits>\n#include "render-utils/render_api_platform_types.h"\n'
                        'static_assert(std::is_same<FBNativeWindowType, uint32_t>::value, "Linux type changed");\n')
        linux_entries = json.loads((args.linux_build.resolve() / "compile_commands.json").read_text())
        linux_entry = next(x for x in linux_entries if x["file"].endswith("/DisplaySurfaceGl.cpp"))
        linux_cxx = shlex.split(linux_entry["command"])[0]
        subprocess.run([linux_cxx, "-std=c++17", "-I", str(source / "include"), "-c", str(test),
                        "-o", str(root / "linux-type.o")], check=True, capture_output=True, text=True, timeout=90)
        print("LINUX_WINDOW_TYPE_UNCHANGED")


if __name__ == "__main__":
    main()
