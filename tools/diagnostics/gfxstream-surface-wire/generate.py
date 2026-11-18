#!/usr/bin/env python3
"""Generate paired KHR_surface codecs from the pinned local upstream source."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path, help="must not already exist")
parser.add_argument("--clang-bin", type=Path)
parser.add_argument("--private-create", action="store_true",
                    help="generate an isolated, unnegotiated private create command")
args = parser.parse_args()
source = args.source.resolve(strict=True)
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
registry = source / "codegen/vulkan/vulkan-docs-next"
shutil.copytree(registry / "scripts", output / "scripts")
shutil.copytree(registry / "xml", output / "xml")
if args.private_create:
    xml = output / "xml/vk_gfxstream.xml"
    data = xml.read_text()
    marker = '    </commands>'
    command = '''        <command successcodes="VK_SUCCESS" errorcodes="VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_INITIALIZATION_FAILED">
            <proto><type>VkResult</type> <name>vkCreateEmuSurfaceRME</name></proto>
            <param><type>VkInstance</type> <name>instance</name></param>
            <param><type>VkSurfaceKHR</type>* <name>pSurface</name></param>
        </command>
'''
    requirement = '                <command name="vkGetBlobGOOGLE"/>'
    if data.count(marker) != 1 or data.count(requirement) != 1:
        raise SystemExit("unexpected gfxstream registry layout")
    data = data.replace(marker, command + marker, 1)
    data = data.replace(requirement,
                        requirement + '\n                <command name="vkCreateEmuSurfaceRME"/>', 1)
    xml.write_text(data)
config = output / "scripts/cerealgenerator.py"
text = config.read_text()
old = '"VK_KHR_surface": ["goldfish_vk_dispatch"],'
new = ('"VK_KHR_surface": HOST_MODULES + ["VkEncoder", '
       '"goldfish_vk_reserved_marshaling_guest", "goldfish_vk_deepcopy_guest", '
       '"goldfish_vk_counting_guest", "goldfish_vk_transform_guest", '
       '"goldfish_vk_marshaling_guest", "goldfish_vk_extension_structs_guest"],')
if text.count(old) != 1:
    raise SystemExit("unexpected surface generator configuration")
config.write_text(text.replace(old, new, 1))
guest = output / "guest"
host = output / "host"
cereal = host / "cereal"
guest.mkdir()
(cereal / "common").mkdir(parents=True)
env = dict(os.environ, GFXSTREAM_GUEST_ENCODER_DIR=str(guest),
           GFXSTREAM_HOST_DECODER_DIR=str(host), GFXSTREAM_OUTPUT_DIR=str(cereal),
           GFXSTREAM_SCRIPTS_DIR=str(source / "scripts"))
if args.clang_bin:
    env["PATH"] = str(args.clang_bin.resolve(strict=True)) + os.pathsep + env["PATH"]
subprocess.run(["python3", str(output / "scripts/genvk.py"),
                "-registry", str(output / "xml/vk.xml"), "-registryGfxstream",
                str(output / "xml/vk_gfxstream.xml"), "cereal", "-o", str(cereal)],
               env=env, check=True)
subprocess.run([sys.executable, str(Path(__file__).with_name("apply_fixes.py")), str(output)], check=True)
if args.private_create:
    subprocess.run([sys.executable, str(Path(__file__).with_name("apply_create.py")), str(output)], check=True)
print("ISOLATED_OUTPUT", output)
