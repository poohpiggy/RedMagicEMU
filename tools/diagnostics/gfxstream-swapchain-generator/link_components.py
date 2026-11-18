#!/usr/bin/env python3
"""Make a relocatable merge and check surface serialization definitions."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('ndk', type=Path)
parser.add_argument('decoder', type=Path)
parser.add_argument('components', type=Path)
parser.add_argument('output', type=Path, help='must not already exist')
parser.add_argument('--surface-adapter-dir', type=Path,
                    help='directory containing the three separately compiled Android surface adapter objects')
args = parser.parse_args()
linker = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/ld.lld'
nm = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm'
names = ['VkDecoderGlobalState.cpp', 'VkDecoderSnapshot.cpp',
         'goldfish_vk_extension_structs.cpp', 'goldfish_vk_marshaling.cpp',
         'goldfish_vk_reserved_marshaling.cpp', 'goldfish_vk_deepcopy.cpp',
         'goldfish_vk_dispatch.cpp', 'goldfish_vk_transform.cpp']
objects = [args.decoder.resolve(strict=True)]
objects += [(args.components / (name + '.o')).resolve(strict=True) for name in names]
if args.surface_adapter_dir:
    objects += [(args.surface_adapter_dir / name).resolve(strict=True) for name in (
        'host_guest_surface_android.o', 'host_surface-android-cpp-link.o',
        'surface_jni-android-cpp-link.o')]
output = args.output.resolve()
if output.exists():
    raise SystemExit('output already exists')
subprocess.run([str(linker), '-r', '-o', str(output), *(str(p) for p in objects)], check=True)
symbols = subprocess.check_output([str(nm), '--undefined-only', '--demangle', str(output)], text=True)
unresolved = [line.strip() for line in symbols.splitlines()
              if 'Surface' in line and any(s in line for s in ('marshal_', 'transform_', 'VkDecoderSnapshot::'))]
if unresolved:
    raise SystemExit('unresolved generated surface symbols: ' + '; '.join(unresolved))
if args.surface_adapter_dir:
    unresolved = [line.strip() for line in symbols.splitlines()
                  if any(s in line for s in ('rme_vk_guest_surface_', 'rme_vk_host_surface_',
                                             'rme_vk_jni_acquire_window'))]
    if unresolved:
        raise SystemExit('unresolved EMU surface adapter symbols: ' + '; '.join(unresolved))
    print('HOST_EMU_SURFACE_ADAPTER_RELOCATABLE_MERGE_OK', output)
print('HOST_SURFACE_SERIALIZERS_RELOCATABLE_MERGE_OK', output)
