#!/usr/bin/env python3
"""Test renderer drain ordering with a real upstream process pipe."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("build", type=Path)
p.add_argument("pipe_object", type=Path)
p.add_argument("--patch", type=Path)
a = p.parse_args()
here = Path(__file__).resolve().parent
channel = here.parent / "gfxstream-androidpipe-channel"
with tempfile.TemporaryDirectory(prefix="rme-renderer-drain-test-") as tmp:
    root = Path(tmp)
    target = root / "host/RendererImpl.cpp"
    target.parent.mkdir()
    target.write_bytes((a.source / "host/RendererImpl.cpp").read_bytes())
    if a.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(a.patch.resolve())], cwd=root, check=True)
        src = target.read_text()
        start = src.index("void RendererImpl::rmeDrainForSession()")
        end = src.index("\nvoid RendererImpl::addListener", start)
        (root / "drain_method.h").write_text(src[start:end])
    command = [str(a.build / "bin/cxx"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread",
               "-I" + str(root), "-I" + str(channel), "-I" + str(a.build / "prefix/include"),
               "-I" + str(a.build / "prefix/include/aemu/host-common"),
               "-I" + str(a.source / "host/gl/gl-host-common/include"),
               "-I" + str(a.build / "aemu/host-common/include/host-common"),
               str(here / "test_drain.cpp"), str(channel / "channel.cpp"), str(a.pipe_object),
               str(a.build / "aemu/host-common/VmLock.cpp"), "-L" + str(a.build / "prefix/lib"),
               "-Wl,-rpath," + str(a.build / "prefix/lib"), "-Wl,--export-dynamic",
               "-lgfxstream_backend", "-laemu-host-common", "-laemu-base", "-laemu-logging",
               "-o", str(root / "test")]
    if a.patch:
        command.insert(1, "-DRME_CANDIDATE")
    subprocess.run(command, check=True)
    raise SystemExit(subprocess.run([str(root / "test")], timeout=5).returncode)
