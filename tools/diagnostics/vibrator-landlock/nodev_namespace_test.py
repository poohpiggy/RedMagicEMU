#!/usr/bin/env python3
"""Self-created Linux mount experiment, never a product sandbox."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def refuse_nested_mounts(root):
    with open("/proc/self/mountinfo", encoding="utf-8") as mounts:
        paths = (line.split()[4] for line in mounts)
        paths = (re.sub(r"\\([0-7]{3})", lambda m: chr(int(m.group(1), 8)), p) for p in paths)
        nested = [p for p in paths if p.startswith(str(root) + "/")]
    if nested:
        raise ValueError("nested mount in test Guest root")


def expect_nested_rejection(root):
    try:
        refuse_nested_mounts(root)
    except ValueError:
        return
    raise AssertionError("nested mount was accepted")


def inside(original_mount_namespace_fd):
    current = os.stat("/proc/self/ns/mnt").st_ino
    original_fd = int(original_mount_namespace_fd)
    original = os.fstat(original_fd).st_ino
    os.close(original_fd)
    if current == original:
        raise RuntimeError("refusing to change mounts outside a new mount namespace")
    # Prevent test mounts from propagating back to the invoking namespace.
    run("mount", "--make-rprivate", "/")
    with tempfile.TemporaryDirectory(prefix="rme-vib-nodev-") as tmp:
        base = Path(tmp)
        root = base / "guest"
        root.mkdir()
        (root / "regular").write_text("ok", encoding="utf-8")
        (root / "sys").mkdir()
        (root / "sys" / "control").touch()
        node = base / "device"
        node.touch()
        mounted = []

        def bind(source, target):
            run("mount", "--bind", str(source), str(target))
            mounted.append(target)

        def unmount(target):
            run("umount", str(target))
            mounted.remove(target)

        try:
            # A single host pseudo-device is bound solely for this local test.
            bind("/dev/zero", node)
            with open(node, "wb", buffering=0) as out:
                out.write(b"x")
            run("mount", "-o", "remount,bind,nodev", str(node))
            try:
                with open(node, "wb"):
                    raise AssertionError("nodev allowed opening a character device")
            except PermissionError:
                pass
            bind(root, root)
            run("mount", "-o", "remount,bind,nodev", str(root))
            refuse_nested_mounts(root)

            # A nested mount keeps its own flags and bypasses the parent nodev.
            control = root / "sys" / "control"
            bind("/dev/zero", control)
            with open(control, "wb", buffering=0) as out:
                out.write(b"x")
            expect_nested_rejection(root)
            unmount(control)

            # A writable directory models a sysfs submount; it is not sysfs.
            model = base / "sysfs-model"
            model.mkdir()
            (model / "control").write_text("control", encoding="utf-8")
            bind(model, root / "sys")
            expect_nested_rejection(root)
            unmount(root / "sys")
            refuse_nested_mounts(root)

            probe = base / "probe"
            source = Path(__file__).with_name("nodev_namespace_probe.c")
            run("cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(probe))
            run(str(probe), str(root), str(node), str(root / "regular"))
            print("NODEV_NAMESPACE_TEST_OK")
        finally:
            for path in reversed(mounted):
                subprocess.run(["umount", str(path)], check=False)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--inside":
        inside(sys.argv[2])
        return 0
    if len(sys.argv) != 1:
        return 2
    # Check the namespace facility separately so a test failure cannot be
    # mistaken for a skip. No mounts are changed in this preflight.
    check = subprocess.run(["unshare", "-Urnm", "true"], capture_output=True)
    if check.returncode:
        print("NODEV_NAMESPACE_SKIPPED: user/mount namespace unavailable", file=sys.stderr)
        return 77
    namespace_fd = os.open("/proc/self/ns/mnt", os.O_RDONLY)
    try:
        return subprocess.run(
            ["unshare", "-Urnm", sys.executable, __file__, "--inside", str(namespace_fd)],
            pass_fds=(namespace_fd,),
        ).returncode
    finally:
        os.close(namespace_fd)


if __name__ == "__main__":
    sys.exit(main())
