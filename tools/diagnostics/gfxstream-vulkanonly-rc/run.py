#!/usr/bin/env python3
"""Use exact generated guest RC encoders to test the isolated host decoder."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("build", type=Path)
p.add_argument("--candidate", action="store_true")
p.add_argument("--real-pipe", type=Path, help="patched host AndroidPipe object")
a = p.parse_args()
here = Path(__file__).resolve().parent
source = (a.source / "guest/renderControl_enc/renderControl_enc.cpp").read_text()
names = ["rcGetHostExtensionsString_enc", "rcGetRendererVersion_enc", "rcSetPuid_enc"]
funcs = []
for name in names:
    start = source.index("\n", source.rfind("\n", 0, source.index(name)) - 1) + 1
    brace = source.index("{", source.index(name))
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    funcs.append(source[start:end])
with tempfile.TemporaryDirectory(prefix="rme-vulkanonly-rc-") as tmp:
    out = Path(tmp)
    (out / "actual_encoder.inc").write_text("\n\n".join(funcs) + "\n")
    guest = a.source / "guest"
    cmd = [str(a.build / "bin/cxx"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
           "-Wno-unused-parameter", "-Wno-misleading-indentation", "-I" + str(out),
           "-I" + str(here), "-I" + str(guest / "iostream/include"),
           "-I" + str(here.parent / "gfxstream-principal-scope"),
           "-I" + str(guest / "OpenglCodecCommon"),
           "-I" + str(guest / "OpenglCodecCommon/include"),
           "-I" + str(guest / "renderControl_enc"), str(here / "test.cpp"),
           str(a.build / "build-guest/guest/OpenglCodecCommon/libcodec_common.a.p/ChecksumCalculator.cpp.o"),
           "-o", str(out / "test")]
    if a.candidate:
        cmd.insert(1, "-DRME_CANDIDATE")
    if a.real_pipe:
        channel = here.parent / "gfxstream-androidpipe-channel"
        cmd[1:1] = ["-DRME_REAL_PIPE", "-I" + str(channel),
                    "-I" + str(a.build / "prefix/include"),
                    "-I" + str(a.build / "prefix/include/aemu/host-common"),
                    "-I" + str(a.source / "host/gl/gl-host-common/include"),
                    "-I" + str(a.build / "aemu/host-common/include/host-common")]
        cmd.extend([str(channel / "channel.cpp"), str(a.real_pipe),
                    str(a.build / "aemu/host-common/VmLock.cpp"),
                    "-L" + str(a.build / "prefix/lib"),
                    "-Wl,-rpath," + str(a.build / "prefix/lib"), "-Wl,--export-dynamic",
                    "-lgfxstream_backend", "-laemu-host-common", "-laemu-base", "-laemu-logging", "-pthread"])
    subprocess.run(cmd, check=True)
    raise SystemExit(subprocess.run([str(out / "test")]).returncode)
