#!/usr/bin/env python3
"""Build the standalone probe using explicitly supplied local tools and key."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build-tools", default="36.0.0")
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--keystore", type=Path, required=True)
    parser.add_argument("--key-alias", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get("RME_PROBE_STORE_PASS") or not os.environ.get("RME_PROBE_KEY_PASS"):
        parser.error("set RME_PROBE_STORE_PASS and RME_PROBE_KEY_PASS in the environment")
    output = args.output.resolve()
    if output.exists():
        parser.error("output already exists; choose another path")
    src = Path(__file__).resolve().parent
    tools = args.sdk.resolve() / "build-tools" / args.build_tools
    jar = args.sdk.resolve() / "platforms" / "android-36" / "android.jar"
    java = args.java_home.resolve()
    env = os.environ.copy()
    env["JAVA_HOME"] = str(java)
    env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")

    def executable(name, batch=False):
        suffix = (".bat" if batch else ".exe") if os.name == "nt" else ""
        return tools / (name + suffix)

    def run(*command):
        subprocess.run([str(item) for item in command], env=env, check=True)

    with tempfile.TemporaryDirectory(prefix="rme-ringtone-probe-") as temp:
        work = Path(temp)
        classes, dex = work / "classes", work / "dex"
        classes.mkdir()
        dex.mkdir()
        unsigned, aligned = work / "unsigned.apk", work / "aligned.apk"
        run(executable("aapt"), "package", "-f", "-M", src / "AndroidManifest.xml",
            "-I", jar, "-F", unsigned)
        javac = java / "bin" / ("javac.exe" if os.name == "nt" else "javac")
        run(javac, "--release", "8", "-Xlint:-options", "-classpath", jar,
            "-d", classes, src / "src" / "app" / "rme" / "probe" / "ReadOnlyActivity.java")
        run(executable("d8", batch=True), "--min-api", "26", "--lib", jar,
            "--output", dex, classes / "app" / "rme" / "probe" / "ReadOnlyActivity.class")
        with zipfile.ZipFile(unsigned, "a") as apk:
            apk.write(dex / "classes.dex", "classes.dex")
        run(executable("zipalign"), "-P", "16", "-f", "4", unsigned, aligned)
        run(executable("apksigner", batch=True), "sign", "--ks", args.keystore.resolve(),
            "--ks-key-alias", args.key_alias, "--ks-pass", "env:RME_PROBE_STORE_PASS",
            "--key-pass", "env:RME_PROBE_KEY_PASS", "--v4-signing-enabled", "false",
            "--out", output, aligned)
    print("Built:", output)


if __name__ == "__main__":
    main()
