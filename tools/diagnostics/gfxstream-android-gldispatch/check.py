#!/usr/bin/env python3
"""Compile fixed GLDispatch.cpp for Android and compare Linux objects; no GPU run."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REL = Path("host/gl/glestranslator/GLcommon/GLDispatch.cpp")
SOURCE_SHA256 = "30b73d6d1968b18c88e3e89d12e1f61524ef407ccde8f9b3a728e888cacf82c8"


def command(build):
    entries = json.loads((build / "compile_commands.json").read_text())
    matches = [e for e in entries if e["file"].endswith(str(REL))]
    if len(matches) != 1:
        raise RuntimeError("expected exactly one GLDispatch.cpp compile command")
    return matches[0]


def compile_one(entry, src, out, extra=()):
    args = shlex.split(entry["command"])
    for flag in ("-o", "-MF", "-MQ"):
        i = args.index(flag)
        args[i + 1] = str(out) if flag != "-MF" else str(out) + ".d"
    args[args.index("-c") + 1] = str(src)
    args[1:1] = list(extra)
    return subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True, timeout=90)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--android-build", required=True, type=Path)
    parser.add_argument("--linux-build", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve() / REL
    if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise RuntimeError("source file does not match the fixed d7a33801 baseline")
    android = command(args.android_build.resolve())
    linux = command(args.linux_build.resolve())
    with tempfile.TemporaryDirectory(prefix="rme-gldispatch-check-") as d:
        root = Path(d)
        old = root / "old" / REL
        new = root / "new" / REL
        for dest in (old, new):
            dest.parent.mkdir(parents=True)
            dest.write_bytes(source.read_bytes())
        baseline = compile_one(android, old, root / "old-android.o")
        if baseline.returncode == 0 or "GL/glx.h" not in baseline.stderr:
            raise RuntimeError("Android baseline did not reproduce missing GL/glx.h: " + baseline.stderr[-2000:])
        print("ANDROID_BASELINE_MISSING_GLX_REPRODUCED", flush=True)
        patch = HERE / "android-glx-guard.patch"
        if not patch.is_file():
            raise AssertionError("Android GLX guard patch is missing")
        subprocess.run(["patch", "-p1", "-i", str(patch)], cwd=root / "new", check=True, capture_output=True, text=True)
        changed = compile_one(android, new, root / "new-android.o")
        if changed.returncode:
            raise RuntimeError("patched Android object failed: " + changed.stderr[-4000:])
        header = subprocess.run(["readelf", "-h", str(root / "new-android.o")],
                                check=True, capture_output=True, text=True).stdout
        if "AArch64" not in header:
            raise AssertionError("Android object is not AArch64")
        print("ANDROID_ARM64_OBJECT_OK", flush=True)
        # This file uses no GLX declarations. The synthetic empty GLX header
        # allows an offline Linux object comparison without a desktop GL SDK.
        include = root / "include" / "GL"
        include.mkdir(parents=True)
        (include / "glx.h").write_text("/* Linux comparison placeholder */\n")
        for label, src in (("old", old), ("new", new)):
            obj = root / (label + "-linux.o")
            built = compile_one(linux, src, obj, ("-I", str(include.parent)))
            if built.returncode:
                raise RuntimeError(label + " Linux compile failed: " + built.stderr[-4000:])
            if str(include / "glx.h") not in Path(str(obj) + ".d").read_text():
                raise AssertionError(label + " Linux compile did not include GLX header")
            subprocess.run(["objcopy", "--strip-debug", str(obj)], check=True)
        left = (root / "old-linux.o").read_bytes()
        right = (root / "new-linux.o").read_bytes()
        if left != right:
            raise AssertionError("Linux objects differ after stripping debug information")
        print("LINUX_OBJECT_IDENTICAL sha256=" + hashlib.sha256(left).hexdigest())


if __name__ == "__main__":
    main()
