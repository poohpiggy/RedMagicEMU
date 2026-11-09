#!/usr/bin/env python3
"""Check the actual APK manifest, DEX, permissions, alignment and signature."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


ALLOWED_CALLS = {
    "Landroid/app/Activity;.<init>", "Landroid/app/Activity;.onCreate",
    "Landroid/content/ContentResolver;.getType",
    "Landroid/media/RingtoneManager;.getActualDefaultRingtoneUri", "Landroid/os/Process;.myUid",
    "Landroid/provider/Settings$System;.canWrite", "Landroid/widget/TextView;.<init>",
    "Landroid/widget/TextView;.setText", "Landroid/widget/TextView;.setTextSize", "Landroid/widget/TextView;.setGravity",
    "Lapp/rme/probe/ReadOnlyActivity;.check", "Lapp/rme/probe/ReadOnlyActivity;.checkSelfPermission",
    "Lapp/rme/probe/ReadOnlyActivity;.getApplicationInfo", "Lapp/rme/probe/ReadOnlyActivity;.getContentResolver",
    "Lapp/rme/probe/ReadOnlyActivity;.setContentView", "Ljava/lang/Class;.getSimpleName",
    "Ljava/lang/Object;.getClass", "Ljava/lang/String;.startsWith", "Ljava/lang/StringBuilder;.<init>",
    "Ljava/lang/StringBuilder;.append", "Ljava/lang/StringBuilder;.toString",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build-tools", default="36.0.0")
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("apk", type=Path)
    args = parser.parse_args()
    tools = args.sdk.resolve() / "build-tools" / args.build_tools
    java = args.java_home.resolve()
    env = os.environ.copy()
    env["JAVA_HOME"] = str(java)
    env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")

    def executable(name, batch=False):
        suffix = (".bat" if batch else ".exe") if os.name == "nt" else ""
        return tools / (name + suffix)

    def run(*command):
        return subprocess.run([str(item) for item in command], env=env, check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace").stdout

    info = run(executable("aapt"), "dump", "badging", args.apk)
    launcher = re.search(r"launchable-activity: name='([^']+)'", info)
    require(launcher is not None, "missing launchable activity")
    require("targetSdkVersion:'36'" in info, "unexpected target SDK")
    permissions = re.findall(r"uses-permission: name='([^']+)'", info)
    require(permissions == ["android.permission.WRITE_SETTINGS"], "unexpected permissions")
    with zipfile.ZipFile(args.apk) as apk:
        require(apk.testzip() is None, "corrupt APK")
        dex_entries = [name for name in apk.namelist() if re.fullmatch(r"classes\d*\.dex", name)]
        require(dex_entries == ["classes.dex"], "unexpected DEX entries")
        with tempfile.TemporaryDirectory(prefix="rme-probe-check-") as temp:
            dex = Path(temp) / "classes.dex"
            dex.write_bytes(apk.read("classes.dex"))
            disassembly = run(executable("dexdump"), "-d", dex)
    descriptor = "L" + launcher.group(1).replace(".", "/") + ";"
    classes = re.findall(r"Class descriptor\s+: '([^']+)'", disassembly)
    require(descriptor in classes, "launcher class missing from APK DEX: " + launcher.group(1))
    require(classes == ["Lapp/rme/probe/ReadOnlyActivity;"], "unexpected DEX classes")
    calls = set(re.findall(r"invoke-.*?\b(L[^ ]+;\.[^:]+):", disassembly))
    require(bool(calls) and calls <= ALLOWED_CALLS, "unexpected calls: " + str(sorted(calls - ALLOWED_CALLS)))
    require("Landroid/widget/TextView;.setGravity" in calls, "missing result text positioning")
    run(executable("zipalign"), "-c", "-P", "16", "4", args.apk)
    run(executable("apksigner", batch=True), "verify", args.apk)
    print("Verified:", args.apk)


if __name__ == "__main__":
    main()
