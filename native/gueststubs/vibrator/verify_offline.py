#!/usr/bin/env python3
"""Compile and check the unregistered vibrator experiment entirely offline.

No artifact is published. Framework stand-ins are temporary and none are
suitable for a Guest build. A claimed system UID never grants authority.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
RADIO_COMPILE_ONLY = HERE.parent / "radio" / "compile-only" / "android" / "os"


def verify_sources():
    root = HERE / "aidl"
    expected = json.loads((HERE / "aidl-sha256.json").read_text(encoding="utf-8"))
    files = {p.relative_to(root).as_posix(): p for p in root.rglob("*.aidl")}
    files[".hash"] = root / ".hash"
    if not expected or set(files) != set(expected):
        raise ValueError("vibrator AIDL snapshot file set differs from manifest")
    for name, path in files.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected[name]:
            raise ValueError(f"vibrator AIDL snapshot digest mismatch: {name}")
    return root


def run(args):
    subprocess.run([str(arg) for arg in args], check=True, timeout=120)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--build-tools", default="36.0.0")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    aidl = sdk / "build-tools" / args.build_tools / ("aidl.exe" if os.name == "nt" else "aidl")
    android = sdk / "platforms" / "android-36" / "android.jar"
    javac, java = shutil.which("javac"), shutil.which("java")
    for name, path in (("aidl", aidl), ("android.jar", android), ("javac", javac), ("java", java)):
        if not path or not Path(path).is_file():
            parser.error(f"missing {name}: {path}")
    root = verify_sources()
    version_hash = (root / ".hash").read_text(encoding="utf-8").strip()

    with tempfile.TemporaryDirectory(prefix="rme-vibrator-offline-") as temporary:
        work = Path(temporary)
        generated, hidden, classes, shim = [work / name for name in ("generated", "hidden", "classes", "shim")]
        for directory in (generated, hidden, classes, shim):
            directory.mkdir()
        for source in sorted(root.rglob("*.aidl")):
            run([aidl, "--lang=java", "--structured", "--stability=vintf", "-v", "2",
                 f"--hash={version_hash}", f"-I{root}", "-o", generated, source])

        # android.jar contains stubs for hidden framework APIs. Only this local
        # stand-in claims system UID; it is never packaged or installed.
        binder = (RADIO_COMPILE_ONLY / "Binder.java").read_text(encoding="utf-8")
        original = "    public Binder() {}"
        replacement = original + "\n    public static int getCallingUid() { return 1000; }"
        if binder.count(original) != 1:
            raise ValueError("unexpected compile-only Binder source")
        (shim / "Binder.java").write_text(binder.replace(original, replacement), encoding="utf-8")
        (shim / "ServiceSpecificException.java").write_text(
            "package android.os; public class ServiceSpecificException extends RuntimeException {"
            "public ServiceSpecificException(int code, String message) { super(message); }}\n",
            encoding="utf-8")
        run([javac, "--release", "8", "-cp", android, "-d", hidden,
             shim / "Binder.java", shim / "ServiceSpecificException.java", RADIO_COMPILE_ONLY / "Parcelable.java"])
        cp = os.pathsep.join(map(str, (hidden, android)))
        sources = sorted(generated.rglob("*.java")) + sorted((HERE / "src").rglob("*.java"))
        sources += sorted((HERE / "tests").rglob("*.java"))
        run([javac, "--release", "8", "-cp", cp, "-d", classes, *sources])
        runtime_cp = os.pathsep.join(map(str, (classes, hidden, android)))
        for test in ("VibrationControllerTest", "VibratorHalOfflineTest", "VibratorRegistryTest"):
            run([java, "-cp", runtime_cp, "app.aemu.stub." + test])

    print("VIBRATOR_OFFLINE_BUILD_OK: no installable output or Guest registration")
    print("The HAL rejects requests even when a LOCAL TEST STUB claims system UID; real permission isolation remains unverified.")


if __name__ == "__main__":
    main()
