#!/usr/bin/env python3
"""Build the API 36 Wi-Fi service JAR without installing or starting it.

Framework stand-ins are compilation inputs only.
"""

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
# Reuse the existing DEX class reader instead of relying on dexdump output text.
spec = importlib.util.spec_from_file_location("radio_verify_offline", HERE.parent / "radio/verify_offline.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

OWNER_PATH = "META-INF/redmagic-wifi-owner"
OWNER_CONTENT = b"RedMagicEMU wifi prototype v1\n"
SERVICE_CLASSES = ("VirtualWifiState", "WifiStateBinder", "WifiNetworkLifecycle", "VirtualWifiNetworkAgent",
                   "WifiSystemContext", "VirtualWifiLinkProperties", "WifiToggleAuthorization",
                   "WifiConnectionCoordinator", "WifiNetworkGate", "WifiGuestNetwork", "WifiStub", "WifiNet", "WifiNet2",
                   "WifiService", "WifiServiceGuard", "WifiConfig", "WifiLink", "LegacyWifi", "Bcast", "Refl", "WifiPrivacy",
                   "WifiEthernetHandoff", "WifiStateBroadcasts", "WifiScanAccess", "WifiSettingsReader", "WifiControlWaiter",
                   "WifiControlReadiness", "WifiPersistentState", "WifiAirplaneModeObserver", "EarlyWifiPublication", "EarlyWifiPublishExperiment",
                   "EarlyWifiProofBinder", "EarlyWifiHandoff", "EarlyHandoffRecovery", "WifiDnsGuard",
                   "WifiDnsNetworkSource", "WifiDnsSession", "WifiDnsTransport", "WifiDnsVerifier", "WifiDnsWire",
                   "RadioGuestPeerCredentials")
CLASS_PATTERN = re.compile(r"Lapp/aemu/stub/([A-Za-z_][A-Za-z0-9_]*)(?:\$[A-Za-z0-9_$]+)?;")


def validate_service_classes(names):
    seen = set()
    for name in names:
        match = CLASS_PATTERN.fullmatch(name)
        if not match or match.group(1) not in SERVICE_CLASSES or name in seen:
            raise ValueError(f"unexpected or duplicate DEX class: {name}")
        seen.add(name)
    missing = {f"Lapp/aemu/stub/{name};" for name in SERVICE_CLASSES} - seen
    if missing:
        raise ValueError(f"missing prototype DEX classes: {', '.join(sorted(missing))}")


def build_jar_bytes(dex):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", zipfile.ZIP_DEFLATED) as jar:
        for name, data in (("classes.dex", dex), (OWNER_PATH, OWNER_CONTENT)):
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            jar.writestr(entry, data)
    return result.getvalue()


def publish_bytes(path, data):
    with path.open("xb") as output:
        output.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build-tools", default="36.0.0")
    parser.add_argument("--java-home", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="new path for prototype JAR")
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("output already exists; choose a new path")
    android = args.sdk / "platforms/android-36/android.jar"
    d8 = args.sdk / "build-tools" / args.build_tools / ("d8.bat" if os.name == "nt" else "d8")
    javac_name = "javac.exe" if os.name == "nt" else "javac"
    javac = str(args.java_home / "bin" / javac_name) if args.java_home else shutil.which("javac")
    sources = [HERE / "src/app/aemu/stub" / (name + ".java") for name in SERVICE_CLASSES]
    hidden = [HERE.parent / "hidden/android/net" / (name + ".java") for name in
              ("NetworkAgent", "NetworkAgentConfig", "NetworkProvider")]
    for name, path in (("android.jar", android), ("d8", d8), ("javac", javac),
                       *(("source", path) for path in sources + hidden)):
        if not path or not Path(path).is_file():
            parser.error(f"missing {name}: {path}")
    environment = verify.jdk_environment(args.java_home)
    with tempfile.TemporaryDirectory(prefix="rme-wifi-prototype-") as temporary:
        work = Path(temporary)
        classes, dex, hidden_classes = work / "classes", work / "dex", work / "hidden"
        classes.mkdir()
        dex.mkdir()
        hidden_classes.mkdir()
        def run(command):
            subprocess.run([str(arg) for arg in command], check=True, timeout=120, env=environment)
        run([javac, "--release", "8", "-cp", android, "-d", hidden_classes, *hidden])
        classpath = os.pathsep.join(map(str, (hidden_classes, android)))
        run([javac, "--release", "8", "-cp", classpath, "-d", classes, *sources])
        archive = work / "classes.jar"
        hidden_archive = work / "hidden.jar"
        verify.archive_classes(classes, archive)
        verify.archive_classes(hidden_classes, hidden_archive)
        run([d8, "--min-api", "36", "--lib", android, "--classpath", hidden_archive, "--output", dex, archive])
        if [p.name for p in dex.glob("classes*.dex")] != ["classes.dex"]:
            raise ValueError("unexpected multidex output")
        data = (dex / "classes.dex").read_bytes()
        names = verify.dex_class_names(data)
        validate_service_classes(names)
        payload = build_jar_bytes(data)
        publish_bytes(args.output, payload)
    print(f"WIFI_PROTOTYPE_BUILD_OK dex_classes={len(names)} sha256={hashlib.sha256(payload).hexdigest()}")
    print("Build only; no installation, startup, or network registration.")


if __name__ == "__main__":
    main()
