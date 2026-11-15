#!/usr/bin/env python3
"""Build an isolated API 36 network-control JAR; never overwrites an output."""
import argparse
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("radio_verify_offline", HERE.parent / "radio/verify_offline.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
OWNER_PATH = "META-INF/redmagic-wifi-net-owner"
OWNER_CONTENT = b"RedMagicEMU wifi network prototype v1\n"
CLASSES = ("NetStub", "NetAgent2", "TargetNetControl")
PATTERN = re.compile(r"Lapp/aemu/stub/([A-Za-z_][A-Za-z0-9_]*)(?:\$[A-Za-z0-9_$]+)?;")


def validate_classes(names):
    seen = set()
    for name in names:
        match = PATTERN.fullmatch(name)
        if not match or match.group(1) not in CLASSES or name in seen:
            raise ValueError(f"unexpected or duplicate DEX class: {name}")
        seen.add(name)
    missing = {f"Lapp/aemu/stub/{name};" for name in CLASSES} - seen
    if missing:
        raise ValueError(f"missing DEX classes: {', '.join(sorted(missing))}")


def jar_bytes(dex):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in (("classes.dex", dex), (OWNER_PATH, OWNER_CONTENT)):
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    return result.getvalue()


def publish_bytes(path, data):
    with path.open("xb") as stream:
        stream.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build-tools", default="36.0.0")
    parser.add_argument("--java-home", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("output already exists; choose a new path")
    android = args.sdk / "platforms/android-36/android.jar"
    d8 = args.sdk / "build-tools" / args.build_tools / ("d8.bat" if os.name == "nt" else "d8")
    javac = str(args.java_home / "bin" / ("javac.exe" if os.name == "nt" else "javac")) if args.java_home else shutil.which("javac")
    sources = [HERE.parent / "src/app/aemu/stub" / (name + ".java") for name in CLASSES]
    hidden = [HERE.parent / "hidden/android/net" / (name + ".java") for name in
              ("NetworkAgent", "NetworkAgentConfig", "NetworkProvider")]
    for name, path in (("android.jar", android), ("d8", d8), ("javac", javac),
                       *(("source", path) for path in sources + hidden)):
        if not path or not Path(path).is_file():
            parser.error(f"missing {name}: {path}")
    environment = verify.jdk_environment(args.java_home)
    with tempfile.TemporaryDirectory(prefix="rme-wifi-network-") as tmp:
        work = Path(tmp)
        classes, dex, stubs = work / "classes", work / "dex", work / "stubs"
        for path in (classes, dex, stubs):
            path.mkdir()
        def run(command):
            subprocess.run([str(arg) for arg in command], check=True, timeout=120, env=environment)
        run([javac, "--release", "8", "-cp", android, "-d", stubs, *hidden])
        run([javac, "--release", "8", "-cp", os.pathsep.join(map(str, (stubs, android))), "-d", classes, *sources])
        archive, stub_archive = work / "classes.jar", work / "stubs.jar"
        verify.archive_classes(classes, archive)
        verify.archive_classes(stubs, stub_archive)
        run([d8, "--min-api", "36", "--lib", android, "--classpath", stub_archive, "--output", dex, archive])
        if [p.name for p in dex.glob("classes*.dex")] != ["classes.dex"]:
            raise ValueError("unexpected multidex output")
        data = (dex / "classes.dex").read_bytes()
        names = verify.dex_class_names(data)
        validate_classes(names)
        payload = jar_bytes(data)
        publish_bytes(args.output, payload)
    print(f"WIFI_NET_PROTOTYPE_BUILD_OK dex_classes={len(names)} sha256={hashlib.sha256(payload).hexdigest()}")
    print("Prototype only; no guest installation or service startup.")


if __name__ == "__main__":
    main()
