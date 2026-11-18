#!/usr/bin/env python3
"""Build and stage the Android Vulkan renderer startup APK dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

from check_headless_exit import check_headless_exit

HERE = Path(__file__).resolve().parent


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, timeout=120, **kwargs)


def build(tools, build_dir, paired_dir, out, shared_memory=False):
    out.mkdir()
    jni = out / "jniLibs/arm64-v8a"
    jni.mkdir(parents=True)
    link = shlex.split((paired_dir / "link-command.txt").read_text())
    if "-Wl,--no-undefined" not in link or not any(arg.endswith("/socket_server.o") for arg in link):
        raise RuntimeError("expected the paired strict link with the process-pipe adapter")
    bin_dir = Path(link[0]).parent
    syslibs = bin_dir.parent / "sysroot/usr/lib/aarch64-linux-android"
    database = json.loads((build_dir / "compile_commands.json").read_text())

    def compile_copy(source, entry_name, name):
        entry, = [row for row in database if row["file"].endswith("/" + entry_name)]
        cwd = Path(entry["directory"])
        command = shlex.split(entry["command"])
        original = (cwd / entry["file"]).resolve()
        path = out / (name + ".o")
        command[1:1] = ["-I", str(original.parent)]
        for flag, value in (("-c", source), ("-o", path), ("-MF", path.with_suffix(".d"))):
            command[command.index(flag) + 1] = str(value)
        run(command, cwd=cwd)
        return path

    upstream = build_dir.parent / "source/host"
    framebuffer = upstream / "FrameBuffer.cpp"
    text = framebuffer.read_text()
    before = "        case PostCmd::Exit:\n            m_postWorker->exit();"
    if text.count(before) != 1:
        raise RuntimeError("unexpected FrameBuffer shutdown case")
    fixed = out / framebuffer.name
    fixed.write_text(text.replace(before, before.replace("m_postWorker->exit();",
                                                          "if (m_postWorker) m_postWorker->exit();")))
    if check_headless_exit(fixed, tools / "bin/cxx") != 0:
        raise RuntimeError("headless shutdown test failed")
    print("HEADLESS_POST_WORKER_EXIT_OK", flush=True)
    index, = [i for i, arg in enumerate(link) if arg.endswith("/FrameBuffer.cpp.o")]
    link[index] = str(compile_copy(fixed, framebuffer.name, framebuffer.stem))

    gl_source = upstream / "gl/gl-host-common/opengles.cpp"
    headless = out / gl_source.name
    run([sys.executable, HERE / "apply_headless.py", gl_source, headless])
    headless_obj = compile_copy(headless, "opengles.cpp", "opengles-headless")
    archive = "host/gl/gl-host-common/libgl_host_common.a"
    ar = bin_dir / "llvm-ar"
    members = run([ar, "t", archive], cwd=build_dir, capture_output=True, text=True).stdout.splitlines()
    member, = [m for m in members if m.endswith("/opengles.cpp.o")]
    replacement = out / "libgl-host-headless.a"
    run([ar, "crsT", replacement, *[headless_obj if m == member else build_dir / m for m in members]],
        cwd=build_dir)
    link[link.index(archive)] = str(replacement)

    if shared_memory:
        index, = [i for i, arg in enumerate(link) if arg.endswith("/vulkan-candidate.a")]
        members = run([ar, "t", link[index]], cwd=build_dir, text=True,
                      capture_output=True).stdout.splitlines()
        replacement_objects = {}
        for name, script in (("VkDecoderGlobalState.cpp.o", "patch_shared_memory_host.py"),
                             ("VkDecoder.o", "patch_guest_vulkan_decoder.py")):
            member, = [Path(item) for item in members if Path(item).name == name]
            template = (member.parent / "command.txt" if name == "VkDecoder.o"
                        else member.with_suffix(".command.txt"))
            command = shlex.split(template.read_text())
            obj = out / member.name
            patched = out / (member.stem.removesuffix(".cpp") + ".cpp")
            run([sys.executable, HERE / script, command[command.index("-c") + 1], patched])
            command[1:1] = ["-I" + str(HERE)]
            for flag, value in (("-c", patched), ("-o", obj), ("-MF", obj.with_suffix(".d"))):
                command[command.index(flag) + 1] = str(value)
            run(command, cwd=build_dir)
            replacement_objects[member] = obj
        archive = out / "vulkan-shared-memory.a"
        run([ar, "crsT", archive, *[replacement_objects.get(Path(member), member) for member in members]],
            cwd=build_dir)
        link[index] = str(archive)
        link.insert(link.index("-Wl,--end-group"), "-landroid")

    adapters = ("renderer_start", "primary_display", "vm_snapshot", "framebuffer_color_registry",
                "renderer_service", "renderer_guest_probe", "shared_memory")
    link[1:1] = [str(compile_copy(HERE / (name + ".cpp"), "FrameBuffer.cpp", name)) for name in adapters]
    for name in ("socket_session", "socket_server", "principal_scope", "RendererImpl", "RenderThread"):
        index, = [i for i, arg in enumerate(link) if arg.endswith("/" + name + ".o")]
        previous = Path(link[index])
        recipe = previous.with_name("command.txt") if name == "RenderThread" else previous.with_suffix(".txt")
        command = shlex.split(recipe.read_text())
        output = out / previous.name
        source = Path(command[command.index("-c") + 1])
        if name == "RendererImpl":
            text = source.read_text()
            include = '#include "RendererImpl.h"'
            start = '    const auto channel =\n        std::make_shared<RenderChannelImpl>(loadStream, virtioGpuContextId);'
            if text.count(include) != 1 or text.count(start) != 1 or text.count("    return channel;") != 1:
                raise RuntimeError("unexpected paired renderer channel source")
            text = text.replace(include, include + '\n#include "principal_scope.h"')
            text = text.replace(start, '    const auto principal = rme::PrincipalScope::peek();\n' + start)
            text = text.replace("    return channel;", """    if (principal) {
        principal->trackWorker(channel, [](const std::shared_ptr<void>& worker) {
            const auto channel = std::static_pointer_cast<RenderChannelImpl>(worker);
            channel->stop();
            channel->renderThread()->wait();
        });
    }
    return channel;""")
            source = out / source.name
            source.write_text(text)
        if name == "RenderThread":
            text = source.read_text()
            previous_call = "RcDecoder rmeRcDecoder(mRmePrincipal, tInfo.m_vkInfo.has_value());"
            negotiated_call = ("RcDecoder rmeRcDecoder(mRmePrincipal, tInfo.m_vkInfo.has_value(),\n"
                               "        FrameBuffer::getFB()->getFeatures().VulkanShaderFloat16Int8.enabled);")
            if text.count(previous_call) == 1:
                text = text.replace(previous_call, negotiated_call)
            elif text.count(negotiated_call) != 1:
                raise RuntimeError("unexpected paired Vulkan-only decoder")
            source = out / source.name
            source.write_text(text)
            decoder = HERE.parent / "gfxstream-vulkanonly-rc/rc_decoder.h"
            (out / "rme_vk_rc_decoder.h").write_bytes(decoder.read_bytes())
        for flag, value in (("-c", source), ("-o", output), ("-MF", output.with_suffix(".o.d"))):
            command[command.index(flag) + 1] = str(value)
        run(command, cwd=build_dir)
        link[index] = str(output)
    backend = jni / "libgfxstream_backend.so"
    link[link.index("-o") + 1] = str(backend)
    link.insert(link.index("-Wl,--end-group"), "-llog")
    run(link, cwd=build_dir)

    executable = jni / "libaemuvkrenderer.so"
    dependencies = {Path(arg).name: Path(arg) for arg in link if Path(arg).name.startswith("libaemu-")
                    and Path(arg).suffix == ".so"}
    run([bin_dir / "aarch64-linux-android29-clang", "-fPIE", "-pie", "-Wl,--no-undefined",
         "-Wl,-z,max-page-size=16384", HERE / "renderer_launcher.c", backend,
         "-Wl,-rpath-link," + str(syslibs), *dependencies.values(), "-o", executable])
    dependencies["libc++_shared.so"] = syslibs / "libc++_shared.so"

    # Follow DT_NEEDED rather than bundling all build-time inputs.
    pending = [executable, backend]
    processed = set()
    manifest = {}
    while pending:
        path = pending.pop()
        if path.name in processed:
            continue
        processed.add(path.name)
        if path.parent != jni:
            output = jni / path.name
            run([bin_dir / "llvm-strip", "--strip-debug", "-o", output, path])
            path = output
        else:
            run([bin_dir / "llvm-strip", "--strip-debug", path])
        elf = run([bin_dir / "llvm-readelf", "-h", "-lW", "-d", path], capture_output=True, text=True).stdout
        alignment = [line.split()[-1] for line in elf.splitlines() if line.lstrip().startswith("LOAD ")]
        if "AArch64" not in elf or not alignment or any(int(n, 16) < 16384 for n in alignment):
            raise RuntimeError("expected an AArch64 binary with 16 KiB LOAD alignment: " + path.name)
        if path == executable and "/system/bin/linker64" not in elf:
            raise RuntimeError("launcher must be an Android PIE executable")
        for dependency in re.findall(r"Shared library: \[([\w.+-]+)\]", elf):
            if dependency in dependencies:
                pending.append(dependencies[dependency])
            elif not (jni / dependency).is_file() and not (syslibs / "29" / dependency).is_file():
                raise RuntimeError(f"{path.name} requires unstaged Android dependency {dependency}")
        manifest[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (out / "sha256.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print("ANDROID_RENDERER_STARTUP_BUNDLE", jni.parent)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tools", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("paired_dir", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--shared-memory", action="store_true")
    args = parser.parse_args()
    build(*(getattr(args, name).resolve() for name in ("tools", "build_dir", "paired_dir", "out")),
          shared_memory=args.shared_memory)
