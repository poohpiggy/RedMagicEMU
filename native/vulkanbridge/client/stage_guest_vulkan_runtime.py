#!/usr/bin/env python3
"""Install or restore the public Vulkan loader in a stopped test Guest copy."""

import argparse
import io
from pathlib import Path
import shlex
import subprocess
import tarfile


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("adb", type=Path)
parser.add_argument("serial")
parser.add_argument("guest_root", help="Guest runtime root relative to the app's private home")
parser.add_argument("bundle", type=Path)
parser.add_argument("--restore", action="store_true")
args = parser.parse_args()

adb = [str(args.adb), "-s", args.serial]
app = "app.redmagicemu"
libdir = args.guest_root.rstrip("/") + "/system/lib64"
loader = "libvulkan.so"
backup = "libvulkan.rme-original.so"
temporary = ".rme-vulkan-stage"
inputs = {"libRmePublicVulkan.so": loader}
for path in sorted(args.bundle.glob("libRme*.so")):
    if path.name not in inputs:
        inputs[path.name] = path.name
inputs["libc++_shared.so"] = "libc++_shared.so"
helpers = [name for name in inputs.values() if name != loader]


def shell(command, check=True):
    text = "cd " + shlex.quote(libdir) + " && " + command
    return subprocess.run(adb + ["shell", "run-as " + app + " sh -c " + shlex.quote(text)],
                          check=check, timeout=15)


if args.restore:
    shell("test -f " + backup + " && mv -f " + backup + " " + loader +
          " && rm -f " + shlex.join(helpers) + " && sha256sum " + loader)
else:
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for source, dest in inputs.items():
            path = args.bundle / source
            entry = tar.gettarinfo(str(path), dest)
            entry.mode = 0o644
            with path.open("rb") as payload:
                tar.addfile(entry, payload)
    absent = " && ".join("test ! -e " + shlex.quote(name) for name in [backup, *helpers])
    shell(absent + " && test -f " + loader + " && mkdir -m 700 " + temporary)
    try:
        subprocess.run(adb + ["exec-in", "run-as", app, "tar", "-xf", "-", "-C",
                              libdir + "/" + temporary],
                       input=archive.getvalue(), check=True, timeout=20)
        complete = " && ".join(
            'test "$(stat -c %s ' + shlex.quote(temporary + "/" + dest) + ' 2>/dev/null)" = ' +
            str((args.bundle / source).stat().st_size)
            for source, dest in inputs.items()
        )
        shell("i=0; until " + complete + "; do i=$((i+1)); "
              "test \"$i\" -lt 40 || exit 1; sleep 0.25; done")
        command = "cp -p " + loader + " " + backup + " && cmp -s " + loader + " " + backup
        for name in [*helpers, loader]:
            command += " && mv -f " + shlex.quote(temporary + "/" + name) + " " + shlex.quote(name)
        shell(command + " && sha256sum " + backup + " " + loader)
    except BaseException:
        shell("test ! -f " + backup + " || (mv -f " + backup + " " + loader +
              " && rm -f " + shlex.join(helpers) + ")", check=False)
        raise
    finally:
        names = [temporary + "/" + name for name in inputs.values()]
        shell("rm -f " + shlex.join(names) + " && rmdir " + temporary)
