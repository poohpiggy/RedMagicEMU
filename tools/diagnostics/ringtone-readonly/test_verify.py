#!/usr/bin/env python3
"""Check that the verifier catches the original incorrect launcher class."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build-tools", default="36.0.0")
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("apk", type=Path)
    args = parser.parse_args()
    src = Path(__file__).resolve().parent
    verify = [sys.executable, str(src / "verify.py"), "--sdk", str(args.sdk),
              "--build-tools", args.build_tools, "--java-home", str(args.java_home)]
    subprocess.run(verify + [str(args.apk)], check=True)
    with tempfile.TemporaryDirectory(prefix="rme-probe-regression-") as temp:
        temp = Path(temp)
        manifest = (src / "AndroidManifest.xml").read_text(encoding="utf-8")
        correct = 'android:name="app.rme.probe.ReadOnlyActivity"'
        if correct not in manifest:
            raise ValueError("expected corrected launcher in source manifest")
        (temp / "AndroidManifest.xml").write_text(
            manifest.replace(correct, 'android:name=".ReadOnlyActivity"'), encoding="utf-8")
        broken = temp / "broken.apk"
        suffix = ".exe" if os.name == "nt" else ""
        aapt = args.sdk / "build-tools" / args.build_tools / ("aapt" + suffix)
        jar = args.sdk / "platforms" / "android-36" / "android.jar"
        subprocess.run([str(aapt), "package", "-f", "-M", str(temp / "AndroidManifest.xml"),
                        "-I", str(jar), "-F", str(broken)], check=True)
        with zipfile.ZipFile(args.apk) as good, zipfile.ZipFile(broken, "a") as bad:
            bad.writestr("classes.dex", good.read("classes.dex"))
        result = subprocess.run(verify + [str(broken)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        if result.returncode == 0 or "launcher class missing from APK DEX" not in result.stdout:
            raise AssertionError("incorrect launcher was not rejected for the expected reason:\n" + result.stdout)
    print("Regression passed: incorrect launcher rejected")


if __name__ == "__main__":
    main()
