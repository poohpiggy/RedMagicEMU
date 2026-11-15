"""Build and run the isolated Guest AHardwareBuffer diagnostic on a debug VM."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True, help="path to adb")
    parser.add_argument("--ndk", required=True, help="Android NDK root")
    parser.add_argument("--serial", required=True, help="adb device serial")
    parser.add_argument("--image-id", required=True, help="current Guest image directory name")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.image_id):
        parser.error("image ID contains unsupported characters")

    adb = [args.adb, "-s", args.serial]
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    compiler = Path(args.ndk) / "toolchains" / "llvm" / "prebuilt" / host / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    name = "rme_glfbo_" + uuid.uuid4().hex
    device_tmp = "/data/local/tmp/" + name
    guest_tmp = "files/images/" + args.image_id + "/root/data/local/tmp/" + name
    guest_output = "files/images/" + args.image_id + "/run/" + name + ".out"

    def call(argv, **kwargs):
        return subprocess.run(argv, check=True, timeout=30, **kwargs)

    status = 1
    try:
        with tempfile.TemporaryDirectory(prefix="rme-glfbo-") as tmp:
            binary = Path(tmp) / "probe"
            call([str(compiler), "--target=aarch64-linux-android29", "-Wl,-z,max-page-size=4096",
                  "-Wall", "-Wextra", "-Werror", "-O1", str(Path(__file__).with_name("probe.c")),
                  "-o", str(binary), "-landroid", "-lEGL", "-lGLESv2", "-llog"])
            digest = hashlib.sha256(binary.read_bytes()).hexdigest()
            call(adb + ["push", str(binary), device_tmp])
            call(adb + ["shell", "run-as", "app.redmagicemu", "cp", device_tmp, guest_tmp])
            output = call(adb + ["shell", "run-as", "app.redmagicemu", "sha256sum", guest_tmp],
                          capture_output=True, text=True).stdout
            if not output.split() or output.split()[0].lower() != digest:
                raise RuntimeError("Guest temporary binary checksum differs from local binary")
            call(adb + ["shell", "run-as", "app.redmagicemu", "chmod", "700", guest_tmp])
            command = "timeout 20 /data/local/tmp/" + name + "; echo PROBE_EXIT:$?"
            call(adb + ["shell", "am", "broadcast", "-a", "app.redmagicemu.SHELL", "-p", "app.redmagicemu",
                        "--es", "cmd", shlex.quote(command), "--es", "out", name + ".out"])
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                result = subprocess.run(adb + ["exec-out", "run-as", "app.redmagicemu", "cat", guest_output],
                                        capture_output=True, text=True, timeout=10)
                if result.returncode == 0 and "<<done>>" in result.stdout:
                    print(result.stdout, end="")
                    status = 0 if "PROBE_EXIT:0\n" in result.stdout else 1
                    break
                time.sleep(1)
            else:
                raise RuntimeError("Guest did not produce a completed diagnostic within 40 seconds")
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(f"diagnostic failed: {error}", file=sys.stderr)
    finally:
        cleanup = [["shell", "run-as", "app.redmagicemu", "rm", "-f", path]
                   for path in (guest_tmp, guest_output, guest_output + ".tmp")]
        cleanup.append(["shell", "rm", "-f", device_tmp])
        for args in cleanup:
            try:
                subprocess.run(adb + args, timeout=10, check=True, capture_output=True)
            except (OSError, subprocess.SubprocessError) as error:
                print(f"temporary-file cleanup failed: {error}", file=sys.stderr)
                status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
