#!/usr/bin/env python3
"""Compile the separate d7a Android GLES encoder source sets, without installation."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shlex
import subprocess
import sys

if len(sys.argv) != 5:
    sys.exit("usage: compile_gles_components.py TEMPLATE_COMMAND SOURCE REAL_HEADERS OUTPUT")
template, source, headers, out = map(Path, sys.argv[1:])
args = shlex.split(template.read_text())
args = [a for a in args if "/android_stub" not in a and not a.startswith("-DLOG_TAG=")]
args.insert(1, "-I" + str(headers))
args.insert(1, "-DGL_GLEXT_PROTOTYPES")
groups = {
    "gles1": ["GLEncoder.cpp", "GLEncoderUtils.cpp", "gl_client_context.cpp", "gl_enc.cpp", "gl_entry.cpp"],
    "gles2": ["GL2EncoderUtils.cpp", "GL2Encoder.cpp", "GLESv2Validation.cpp", "gl2_client_context.cpp", "gl2_enc.cpp", "gl2_entry.cpp", "IOStream2.cpp"],
}

def compile_one(group, file):
    target = out / group
    target.mkdir(parents=True, exist_ok=True)
    path = source / ("guest/GLESv1_enc" if group == "gles1" else "guest/GLESv2_enc") / file
    stem = path.stem
    command = args.copy()
    command.insert(1, '-DLOG_TAG="emuglGLESv1_enc"' if group == "gles1" else '-DLOG_TAG="emuglGLESv2_enc"')
    command[command.index("-c") + 1] = str(path)
    command[command.index("-o") + 1] = str(target / (stem + ".o"))
    command[command.index("-MF") + 1] = str(target / (stem + ".d"))
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (target / (stem + ".log")).write_text(result.stdout)
    if result.returncode == 0 and "android_stub/" in (target / (stem + ".d")).read_text():
        raise ValueError("unexpected Mesa stub headers")
    return group, file, result.returncode

jobs = [(group, file) for group, files in groups.items() for file in files]
with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(lambda job: compile_one(*job), jobs))
for group, file, status in results:
    print(f"{group}/{file}: {status}")
if any(status for _, _, status in results):
    sys.exit(1)
