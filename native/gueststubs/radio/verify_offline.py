#!/usr/bin/env python3
"""Build and test the isolated radio prototypes, without downloading or installing anything.

The generated Android interfaces and host shims are compilation inputs only. The test DEX
is never a production service and must not be installed as a replacement framework.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile


HERE = Path(__file__).resolve().parent
GUESTSTUBS = HERE.parent
SOURCES = {
    "base": HERE / "aidl/base",
    "voice": HERE / "aidl/voice",
    "messaging": GUESTSTUBS / "radio-messaging/aidl/messaging",
    "config": GUESTSTUBS / "radio-config/aidl/config",
    "modem": GUESTSTUBS / "radio-modem-sim/aidl/modem",
    "sim": GUESTSTUBS / "radio-modem-sim/aidl/sim",
    "network": GUESTSTUBS / "radio-network/aidl/network",
    "data": GUESTSTUBS / "radio-data/aidl/data",
}
SERVICES = [
    HERE / "src", GUESTSTUBS / "radio-messaging/src",
    GUESTSTUBS / "radio-config/src", GUESTSTUBS / "radio-modem-sim/src",
    GUESTSTUBS / "radio-network/src", GUESTSTUBS / "radio-bridge/src",
    GUESTSTUBS / "radio-data/src",
]
TEST_DIRS = [
    HERE / "tests", GUESTSTUBS / "radio-messaging/tests",
    GUESTSTUBS / "radio-config/tests", GUESTSTUBS / "radio-modem-sim/tests",
    GUESTSTUBS / "radio-network/tests", GUESTSTUBS / "radio-bridge/tests",
    GUESTSTUBS / "radio-data/tests",
]
# Cross-process Binder tests require a Guest service manager; never run them on the host.
GUEST_ONLY = {"RadioVoiceBinderContractTest", "RadioTraceBinderTest", "RadioSyntheticProcessTest", "RadioVintfCanaryTest", "RadioMessagingBinderTest", "RadioSimSpnGuestTest", "RadioEventSocketTest", "RadioSmsPduGuestTest", "RadioDataGuestParcelTest"}
TEST_MODES = {"RadioVoiceRebindRaceTest": ("direct", "full"),
              "RadioVoiceTimeoutCloseTest": ("timeout", "close")}


def run(args, env):
    subprocess.run([str(a) for a in args], check=True, timeout=120, env=env)


def jdk_environment(java_home):
    environment = os.environ.copy()
    if java_home is not None:
        selected = java_home.resolve()
        environment["JAVA_HOME"] = str(selected)
        environment["PATH"] = str(selected / "bin") + os.pathsep + environment.get("PATH", "")
    return environment


def verify_frozen_sources(sources, manifest):
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(expected, dict) or not expected:
        raise ValueError("invalid AIDL SHA-256 manifest")
    files = {}
    for name, source in sources.items():
        paths = list(source.rglob("*.aidl")) + [source / ".hash"]
        for path in paths:
            if path.is_file():
                files[f"{name}/{path.relative_to(source).as_posix()}"] = path
    if set(files) != set(expected):
        raise ValueError("frozen AIDL file set differs from manifest")
    for name, path in files.items():
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != expected[name]:
            raise ValueError(f"frozen AIDL SHA-256 mismatch: {name}")


def archive_classes(source, destination):
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as jar:
        for path in sorted(source.rglob("*.class")):
            jar.write(path, path.relative_to(source).as_posix())


def dex_class_names(data):
    if data[:4] != b"dex\n" or len(data) < 112:
        raise ValueError("not a DEX file")
    word = lambda offset: struct.unpack_from("<I", data, offset)[0]
    string_count, string_offset = word(56), word(60)
    type_count, type_offset = word(64), word(68)
    class_count, class_offset = word(96), word(100)

    def string(index):
        if index >= string_count:
            raise ValueError("invalid DEX string index")
        pos = word(string_offset + index * 4)
        while data[pos] & 0x80:
            pos += 1
        pos += 1
        return data[pos:data.index(b"\0", pos)].decode("utf-8")

    names = []
    for i in range(class_count):
        index = word(class_offset + i * 32)
        if index >= type_count:
            raise ValueError("invalid DEX type index")
        names.append(string(word(type_offset + index * 4)))
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True, help="local Android SDK directory")
    parser.add_argument("--build-tools", default="36.0.0", help="installed SDK build-tools version")
    parser.add_argument("--java-home", type=Path, help="JDK home; otherwise use PATH")
    parser.add_argument("--output", type=Path, help="optional NEW path for the isolated test jar")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    tools = sdk / "build-tools" / args.build_tools
    aidl = tools / ("aidl.exe" if os.name == "nt" else "aidl")
    d8 = tools / ("d8.bat" if os.name == "nt" else "d8")
    android = sdk / "platforms/android-36/android.jar"
    java_bin = args.java_home.resolve() / "bin" if args.java_home else None
    javac = str(java_bin / ("javac.exe" if os.name == "nt" else "javac")) if java_bin else shutil.which("javac")
    java = str(java_bin / ("java.exe" if os.name == "nt" else "java")) if java_bin else shutil.which("java")
    for name, path in (("aidl", aidl), ("d8", d8), ("android.jar", android), ("javac", javac), ("java", java)):
        if not path or not Path(path).is_file():
            parser.error(f"missing {name}: {path}")
    if args.output and args.output.exists():
        parser.error("output already exists; choose a new path")
    verify_frozen_sources(SOURCES, HERE / "aidl-sha256.json")
    environment = jdk_environment(args.java_home)

    with tempfile.TemporaryDirectory(prefix="rme-radio-offline-") as temporary:
        work = Path(temporary)
        generated, hidden, interfaces, app, dex = [work / n for n in ("generated", "hidden", "interfaces", "app", "dex")]
        for directory in (generated, hidden, interfaces, app, dex):
            directory.mkdir()
        includes = [f"-I{path}" for path in SOURCES.values()]
        for source in SOURCES.values():
            version_hash = (source / ".hash").read_text(encoding="utf-8").strip()
            for item in sorted(source.rglob("*.aidl")):
                run([aidl, "--lang=java", "--structured", "--stability=vintf", "-v", "4",
                     f"--hash={version_hash}", *includes, "-o", generated, item], environment)

        # android.jar's host RemoteException methods throw Stub!; this test-only shim is
        # never packaged. The remaining two shims are checked-in compile-only signatures.
        remote = work / "RemoteException.java"
        remote.write_text("package android.os; public class RemoteException extends Exception {"
                          "public RemoteException() {} public RemoteException(String m) { super(m); }}\n",
                          encoding="utf-8")
        hidden_sources = sorted((HERE / "compile-only").rglob("*.java")) + [remote]
        run([javac, "--release", "8", "-cp", android, "-d", hidden, *hidden_sources], environment)
        hidden_cp = os.pathsep.join(map(str, (hidden, android)))
        run([javac, "--release", "8", "-cp", hidden_cp, "-d", interfaces,
             *sorted(generated.rglob("*.java"))], environment)
        cp = os.pathsep.join(map(str, (hidden, interfaces, android)))
        service_sources = [p for source in SERVICES for p in sorted(source.rglob("*.java"))]
        test_sources = [p for source in TEST_DIRS for p in sorted(source.rglob("*.java"))]
        run([javac, "--release", "8", "-cp", cp, "-d", app, *service_sources, *test_sources], environment)
        host_cp = os.pathsep.join(map(str, (hidden, interfaces, app, android)))
        for test in test_sources:
            if test.stem not in GUEST_ONLY:
                for mode in TEST_MODES.get(test.stem, (None,)):
                    print(f"HOST_TEST {test.stem}" + (f" {mode}" if mode else ""), flush=True)
                    run([java, "-cp", host_cp, f"app.aemu.stub.{test.stem}"] + ([mode] if mode else []), environment)

        interface_jar, app_jar = work / "interfaces.jar", work / "app.jar"
        archive_classes(interfaces, interface_jar)
        archive_classes(app, app_jar)
        run([d8, "--min-api", "36", "--lib", android, "--classpath", interface_jar,
             "--output", dex, app_jar], environment)
        if [p.name for p in dex.glob("classes*.dex")] != ["classes.dex"]:
            raise ValueError("unexpected multidex output")
        data = (dex / "classes.dex").read_bytes()
        names = dex_class_names(data)
        if not names or any(not name.startswith("Lapp/aemu/stub/") for name in names):
            raise ValueError("DEX must contain only app.aemu.stub test/service classes")
        jar_path = work / "radio-isolated-tests.jar"
        with zipfile.ZipFile(jar_path, "w", zipfile.ZIP_DEFLATED) as jar:
            entry = zipfile.ZipInfo("classes.dex", (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            jar.writestr(entry, data)
        digest = hashlib.sha256(jar_path.read_bytes()).hexdigest()
        if args.output:
            # Exclusive creation prevents overwriting a user file.
            with args.output.open("xb") as target:
                target.write(jar_path.read_bytes())
        print(f"OFFLINE_RADIO_OK dex_classes={len(names)} sha256={digest}")
        print("Guest-only Binder tests were compiled, not executed; no service was registered.")


if __name__ == "__main__":
    main()
