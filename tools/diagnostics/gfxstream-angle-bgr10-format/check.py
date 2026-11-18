#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Recompile real source and run unmodified format-conversion function slices."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
EXPECTED = {
    "host/common/include/gfxstream/host/gl_enums.h": "f703a6fdf5ac2445d86a25dc8ec66bd233ef95c8a3da7548a4f7c000e1154ee6",
    "host/gl/color_buffer_gl.h": "58f10433c165c833113880a2c0361d4e14c6fca2f9679aee933c44fa1498297d",
    "host/frame_buffer.cpp": "99d35d6469e72b42a301a7e8c501e4f596e224f161617ff013ee787c8a718c55",
}
MARKER = "GL_BGR10_A2_ANGLEX"


def row_for(build):
    rows = json.loads((build / "compile_commands.json").read_text())
    return next(x for x in rows if x.get("output", "").endswith("/frame_buffer.cpp.o"))


def command_for(row, source, target, output, gles, link_lib=None):
    cwd = Path(row["directory"])
    oldroot = (cwd / row["file"]).resolve().parent.parent
    args = shlex.split(row["command"])
    command = []
    count = 0
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-o", "-MF", "-MQ", "-MT"):
            i += 2
            continue
        if arg in ("-MD", "-MMD") or (link_lib is not None and arg == "-c"):
            i += 1
            continue
        if arg == row["file"]:
            arg = str(target)
        elif arg.startswith("-DGFXSTREAM_ENABLE_HOST_GLES="):
            arg = "-DGFXSTREAM_ENABLE_HOST_GLES=" + str(gles)
            count += 1
        elif arg.startswith("-I") and len(arg) > 2:
            path = (cwd / arg[2:]).resolve()
            if path.is_relative_to(oldroot):
                arg = "-I" + str(source / path.relative_to(oldroot))
        command.append(arg)
        i += 1
    if count != 1:
        raise ValueError("unexpected GLES define count")
    if link_lib is not None:
        command.append(str(link_lib))
    elif gles == 0 and "android" in str(cwd).lower():
        command.append("-ferror-limit=0")
    command += ["-o", str(output)]
    return cwd, command


def compile_one(row, source, target, out, label, gles, expected, link_lib=None):
    cwd, command = command_for(row, source, target, out / label, gles, link_lib)
    (out / (label + ".command")).write_text("cd " + shlex.quote(str(cwd)) + "\n" + shlex.join(command) + "\n")
    run = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, timeout=120)
    (out / (label + ".log")).write_text(run.stdout)
    if expected == "ok" and run.returncode:
        raise RuntimeError(f"{label}: unexpected compile failure")
    if expected == "missing" and (run.returncode != 1 or MARKER not in run.stdout):
        raise RuntimeError(f"{label}: expected missing enum compile failure")
    if expected == "other-errors" and (run.returncode != 1 or
            "use of undeclared identifier 'GL_BGR10_A2_ANGLEX'" in run.stdout):
        raise RuntimeError(f"{label}: expected other errors without missing enum")
    print(f"{label}: {expected}", flush=True)


def make_mapping_source(source, dest):
    text = (source / "host/frame_buffer.cpp").read_text()
    start = text.index("std::optional<GfxstreamFormat> GetGfxstreamFormat(")
    end = text.index("static std::optional<std::array<float, 16>> GetColorTransform(", start)
    functions = text[start:end]
    if functions.count("case GL_BGR10_A2_ANGLEX:") != 2:
        raise ValueError("unexpected format conversion functions")
    prefix = '''#include <cstdio>\n#include "frame_buffer.h"\n#include "gfxstream/host/features.h"\n#include "gfxstream/common/logging.h"\n#if GFXSTREAM_ENABLE_HOST_GLES\n#include "gl/color_buffer_gl.h"\n#endif\nnamespace gfxstream { namespace host { namespace {\n'''
    suffix = '''\n}}}\nint main(){\n using namespace gfxstream::host; FeatureSet f;\n auto a=GetGfxstreamFormat(f,GL_BGR10_A2_ANGLEX,FRAMEWORK_FORMAT_GL_COMPATIBLE);\n auto b=GetGfxstreamFormat(f,GL_BGR10_A2_ANGLEX,static_cast<GLenum>(GL_UNSIGNED_BYTE));\n auto c=GetGfxstreamFormat(f,GL_RGBA,FRAMEWORK_FORMAT_GL_COMPATIBLE);\n auto d=GetGfxstreamFormat(f,static_cast<GLenum>(0xdeadbeef),FRAMEWORK_FORMAT_GL_COMPATIBLE);\n if (!a || !b || *a!=GfxstreamFormat::B10G10R10A2_UNORM || *b!=*a || !c || *c!=GfxstreamFormat::R8G8B8A8_UNORM || d) return 1;\n std::printf("ANGLE_VALUE=%x TWO_OVERLOADS_OK RGBA_CONTROL_OK INVALID_REJECTED\\n", GL_BGR10_A2_ANGLEX); return 0;\n}\n'''
    dest.write_text(prefix + functions + suffix)


def main():
    if len(sys.argv) != 6:
        print("usage: check.py SCAFFOLDED_SOURCE ANDROID_BUILD LINUX_GLES_BUILD BASELINE_LIBDIR DEP_LIBDIR", file=sys.stderr)
        return 2
    source, android_build, linux_build, libdir, deps = map(Path, sys.argv[1:])
    if not all(p.is_absolute() for p in (source, android_build, linux_build, libdir, deps)):
        raise ValueError("all paths must be absolute")
    for name, expected in EXPECTED.items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"unexpected source hash: {name}")
    lib = libdir / "libgfxstream_backend.so.0.1.2"
    if not lib.is_file() or (libdir / "libgfxstream_backend.so.0").resolve(strict=True) != lib.resolve(strict=True):
        raise ValueError("missing baseline library or inconsistent SONAME link")
    android, linux = row_for(android_build), row_for(linux_build)
    out = Path(tempfile.mkdtemp(prefix="rme-angle-bgr10-", dir="/var/tmp"))
    print("artifacts=" + str(out), flush=True)
    copied = out / "source"
    shutil.copytree(source, copied, symlinks=False)
    for phase in ("baseline", "candidate"):
        if phase == "candidate":
            with (HERE / "move-angle-format.patch").open("rb") as patch:
                subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(copied)],
                               stdin=patch, check=True, timeout=30)
        compile_one(android, copied, HERE / "visibility.cpp", out, phase + "-android-visibility", 0,
                    "missing" if phase == "baseline" else "ok")
        compile_one(android, copied, copied / "host/frame_buffer.cpp", out, phase + "-android-frame", 0,
                    "missing" if phase == "baseline" else "other-errors")
        compile_one(linux, copied, HERE / "visibility.cpp", out, phase + "-linux-visibility", 1, "ok")
        compile_one(linux, copied, copied / "host/frame_buffer.cpp", out, phase + "-linux-frame", 1, "ok")
        mapping = out / (phase + "-mapping.cpp")
        make_mapping_source(copied, mapping)
        for gles in (0, 1):
            label = f"{phase}-mapping-gles{gles}"
            expected = "missing" if phase == "baseline" and gles == 0 else "ok"
            compile_one(linux, copied, mapping, out, label, gles, expected, lib)
            if expected == "ok":
                env = os.environ.copy()
                env["LD_LIBRARY_PATH"] = f"{libdir}:{deps}"
                run = subprocess.run([str(out / label)], env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, timeout=30)
                (out / (label + ".run.log")).write_text(run.stdout)
                if run.returncode or "ANGLE_VALUE=6af9 TWO_OVERLOADS_OK RGBA_CONTROL_OK INVALID_REJECTED" not in run.stdout:
                    raise RuntimeError(f"{label}: format conversion failed")
    print("ANGLE_FORMAT_CHECK_OK (Android object still fails; no device run)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, StopIteration, subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
