#!/usr/bin/env python3
"""Build a standalone experimental API 36 Radio service JAR without installing it.

The output contains service code only. Frozen Android interfaces and compile-only
framework signatures remain classpath inputs and are never packaged.
"""

import argparse
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import tempfile
import zipfile


HERE = Path(__file__).resolve().parent
GUESTSTUBS = HERE.parent
_spec = importlib.util.spec_from_file_location("radio_verify_offline", HERE / "verify_offline.py")
_verify = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_verify)

SERVICE_SOURCE_PATHS = (
    HERE / "src/app/aemu/stub/RadioServiceMain.java",
    HERE / "src/app/aemu/stub/RadioSlot1Registry.java",
    HERE / "src/app/aemu/stub/RadioVoiceService.java",
    HERE / "src/app/aemu/stub/RadioRequestCounter.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioEventEndpoint.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioEventSession.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioEventSessionManager.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioEventSessionPump.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioEventSocketTransport.java",
    GUESTSTUBS / "radio-bridge/src/app/aemu/stub/RadioGuestPeerCredentials.java",
    GUESTSTUBS / "radio-config/src/app/aemu/stub/RadioConfigService.java",
    GUESTSTUBS / "radio-config/src/app/aemu/stub/SyntheticSingleSlot.java",
    GUESTSTUBS / "radio-config/src/app/aemu/stub/SyntheticSimProfile.java",
    GUESTSTUBS / "radio-modem-sim/src/app/aemu/stub/RadioModemService.java",
    GUESTSTUBS / "radio-modem-sim/src/app/aemu/stub/RadioSimService.java",
    GUESTSTUBS / "radio-network/src/app/aemu/stub/RadioNetworkService.java",
    GUESTSTUBS / "radio-data/src/app/aemu/stub/RadioDataService.java",
    GUESTSTUBS / "radio-messaging/src/app/aemu/stub/RadioMessagingService.java",
)
SERVICE_CLASSES = tuple(path.stem for path in SERVICE_SOURCE_PATHS)
CLASS_PATTERN = re.compile(r"Lapp/aemu/stub/([A-Za-z_][A-Za-z0-9_]*)(?:\$[A-Za-z0-9_$]+)?;")
OWNER_PATH = "META-INF/redmagic-radio-owner"
OWNER_CONTENT = b"RedMagicEMU radio-service v1\n"


def service_sources():
    return list(SERVICE_SOURCE_PATHS)


def validate_service_classes(names):
    seen = set()
    for name in names:
        match = CLASS_PATTERN.fullmatch(name)
        if match is None or match.group(1) not in SERVICE_CLASSES:
            raise ValueError(f"unexpected class in service DEX: {name}")
        if name in seen:
            raise ValueError(f"duplicate class in service DEX: {name}")
        seen.add(name)
    missing = {f"Lapp/aemu/stub/{name};" for name in SERVICE_CLASSES} - seen
    if missing:
        raise ValueError(f"service DEX missing required classes: {', '.join(sorted(missing))}")


def publish_bytes(path, data):
    # Exclusive creation also rejects existing and dangling links; never overwrite.
    with path.open("xb") as output:
        output.write(data)


def build_jar_bytes(dex):
    result = io.BytesIO()
    with zipfile.ZipFile(result, "w", zipfile.ZIP_DEFLATED) as jar:
        for name, data in (("classes.dex", dex), (OWNER_PATH, OWNER_CONTENT)):
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            jar.writestr(entry, data)
    return result.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True, help="local Android SDK")
    parser.add_argument("--build-tools", default="36.0.0", help="installed SDK build-tools version")
    parser.add_argument("--java-home", type=Path, help="JDK home; otherwise use PATH")
    parser.add_argument("--output", type=Path, required=True, help="NEW path for the experimental service JAR")
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("output already exists; choose a new path")
    sdk = args.sdk.resolve()
    tools = sdk / "build-tools" / args.build_tools
    aidl = tools / ("aidl.exe" if os.name == "nt" else "aidl")
    d8 = tools / ("d8.bat" if os.name == "nt" else "d8")
    android = sdk / "platforms/android-36/android.jar"
    javac_name = "javac.exe" if os.name == "nt" else "javac"
    javac = str(args.java_home.resolve() / "bin" / javac_name) if args.java_home else shutil.which("javac")
    for name, path in (("aidl", aidl), ("d8", d8), ("android.jar", android), ("javac", javac)):
        if not path or not Path(path).is_file():
            parser.error(f"missing {name}: {path}")
    for path in service_sources():
        if not path.is_file():
            parser.error(f"missing service source: {path}")
    _verify.verify_frozen_sources(_verify.SOURCES, HERE / "aidl-sha256.json")
    env = _verify.jdk_environment(args.java_home)

    with tempfile.TemporaryDirectory(prefix="rme-radio-service-") as temporary:
        work = Path(temporary)
        generated, hidden, interfaces, app, dex = [work / name for name in ("generated", "hidden", "interfaces", "app", "dex")]
        for directory in (generated, hidden, interfaces, app, dex):
            directory.mkdir()
        includes = [f"-I{path}" for path in _verify.SOURCES.values()]
        for source in _verify.SOURCES.values():
            version_hash = (source / ".hash").read_text(encoding="utf-8").strip()
            for item in sorted(source.rglob("*.aidl")):
                _verify.run([aidl, "--lang=java", "--structured", "--stability=vintf", "-v", "4",
                             f"--hash={version_hash}", *includes, "-o", generated, item], env)
        _verify.run([javac, "--release", "8", "-cp", android, "-d", hidden,
                     *sorted((HERE / "compile-only").rglob("*.java"))], env)
        hidden_cp = os.pathsep.join(map(str, (hidden, android)))
        _verify.run([javac, "--release", "8", "-cp", hidden_cp, "-d", interfaces,
                     *sorted(generated.rglob("*.java"))], env)
        service_cp = os.pathsep.join(map(str, (hidden, interfaces, android)))
        _verify.run([javac, "--release", "8", "-cp", service_cp, "-d", app, *service_sources()], env)
        interface_jar, service_jar = work / "interfaces.jar", work / "service-classes.jar"
        _verify.archive_classes(interfaces, interface_jar)
        _verify.archive_classes(app, service_jar)
        _verify.run([d8, "--min-api", "36", "--lib", android, "--classpath", interface_jar,
                     "--output", dex, service_jar], env)
        if [p.name for p in dex.glob("classes*.dex")] != ["classes.dex"]:
            raise ValueError("unexpected multidex output")
        data = (dex / "classes.dex").read_bytes()
        names = _verify.dex_class_names(data)
        validate_service_classes(names)
        payload = build_jar_bytes(data)
        publish_bytes(args.output, payload)
        print(f"RADIO_SERVICE_BUILD_OK dex_classes={len(names)} sha256={hashlib.sha256(payload).hexdigest()}")
        print("Experimental artifact only; not installed, started, or registered.")


if __name__ == "__main__":
    main()
