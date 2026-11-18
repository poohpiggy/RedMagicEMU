#!/usr/bin/env python3
"""Relink the pinned Android clvk for the Guest Vulkan bridge."""

import argparse
from pathlib import Path
import shlex
import subprocess


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("ninja", type=Path)
parser.add_argument("build", type=Path, help="Built Android clvk CMake directory")
parser.add_argument("output", type=Path)
args = parser.parse_args()
build = args.build.resolve()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def recipe(target):
    output = subprocess.check_output([args.ninja, "-C", build, "-t", "commands", target], text=True)
    return shlex.split(output.splitlines()[-1])


def compile_source(name, edit):
    obj = f"src/CMakeFiles/OpenCL-objects.dir/{name}.cpp.o"
    command = recipe(obj)
    source_arg = command.index("-c") + 1
    upstream = Path(command[source_arg])
    source = out / upstream.name
    source.write_text(edit(upstream.read_text()))
    patched_obj = out / (name + ".cpp.o")
    command[source_arg] = str(source)
    command[1:1] = ["-I" + str(upstream.parent)]
    command[command.index("-o") + 1] = str(patched_obj)
    command[command.index("-MF") + 1] = str(patched_obj.with_suffix(".d"))
    subprocess.run(command, check=True, cwd=build, timeout=180)
    return obj, str(patched_obj)


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected clvk source near " + old)
    return text.replace(old, new)


def core_features(text):
    text = replace(text, "    // Enable cl_khr_fp16 if we have 16-bit storage and shaderFloat16",
                   """    const bool core11 = m_properties.apiVersion >= VK_API_VERSION_1_1;
    const bool core12 = m_properties.apiVersion >= VK_API_VERSION_1_2;
    // On core Vulkan, the storage and shader features need no extension name.
    // Still require the corresponding feature bits reported by the device.""")
    for width, core in ((16, "core11"), (8, "core12")):
        storage = f"is_vulkan_extension_enabled(VK_KHR_{width}BIT_STORAGE_EXTENSION_NAME)"
        text = replace(text, f"if (({storage} &&", f"if ((({core} || {storage}) &&")
    for feature in ("shaderFloat16", "shaderInt8"):
        text = replace(text, f"""(is_vulkan_extension_enabled(
             VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) &&
         m_features_float16_int8.{feature})""",
                       f"""((core12 || is_vulkan_extension_enabled(
             VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) &&
         m_features_float16_int8.{feature})""")
    return text


def unmap_access(text):
    return replace(text, """    if (m_buffer->has_flags(CL_MEM_USE_HOST_PTR)) {
        auto src = m_buffer->host_ptr();
        src = pointer_offset(src, mapping.offset);""",
                   """    if (m_buffer->has_flags(CL_MEM_USE_HOST_PTR) &&
        (mapping.flags & (CL_MAP_WRITE | CL_MAP_WRITE_INVALIDATE_REGION))) {
        auto src = m_buffer->host_ptr();
        src = pointer_offset(src, mapping.offset);""")


objects = [compile_source("device", core_features), compile_source("queue", unmap_access)]

link = recipe("libOpenCL.so")
if link[:2] != [":", "&&"] or link[-2:] != ["&&", ":"]:
    raise ValueError("unexpected clvk CMake link command")
link = link[2:-2]
for obj, patched in objects:
    link[link.index(obj)] = patched
library = out / "libOpenCL.so"
link[link.index("-o") + 1] = str(library)
subprocess.run(link, check=True, cwd=build, timeout=180)
strip = Path(link[0]).with_name("llvm-strip")
subprocess.run([strip, "--strip-debug", library], check=True, timeout=60)
print(library)
