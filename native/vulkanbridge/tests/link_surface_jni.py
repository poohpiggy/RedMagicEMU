#!/usr/bin/env python3
"""Strict-link a standalone Android JNI candidate; never loads or deploys it."""
import pathlib
import re
import subprocess
import sys

ndk, out = (pathlib.Path(arg).resolve(strict=True) for arg in sys.argv[1:])
native = pathlib.Path(__file__).resolve().parent.parent
tools = ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin'
cc, nm, readelf = (tools/name for name in ('aarch64-linux-android26-clang', 'llvm-nm', 'llvm-readelf'))
artifact = out/'libaemu-vksurface-candidate.so'
base = [str(cc), '-std=c11', '-Wall', '-Wextra', '-Werror', '-fPIC', '-fvisibility=hidden',
        '-shared', '-DVK_USE_PLATFORM_ANDROID_KHR', '-Wl,--no-undefined',
        '-Wl,-soname,libaemu-vksurface-candidate.so', str(native/'surface_jni.c'),
        str(native/'host_surface.c')]
baseline = subprocess.run(base + ['-o', str(out/'libaemu-vksurface-missing-android.so')],
                          text=True, capture_output=True)
if baseline.returncode == 0 or not all(name in baseline.stderr for name in (
        'ANativeWindow_acquire', 'ANativeWindow_fromSurface', 'ANativeWindow_release')):
    raise RuntimeError('expected missing libandroid link failure was not reproduced')
print('JNI_LINK_WITHOUT_LIBANDROID_REJECTED', flush=True)
subprocess.run(base + ['-landroid', '-o', str(artifact)], check=True)
header = subprocess.check_output([str(readelf), '-h', str(artifact)], text=True)
dynamic = subprocess.check_output([str(readelf), '-d', str(artifact)], text=True)
exports = subprocess.check_output([str(nm), '-D', '--defined-only', str(artifact)], text=True)
imports = subprocess.check_output([str(nm), '-D', '--undefined-only', str(artifact)], text=True)
if not re.search(r'Class:\s+ELF64', header) or not re.search(r'Machine:\s+AArch64', header):
    raise RuntimeError('output is not an ARM64 ELF shared object')
needed = set(re.findall(r'Shared library: \[(.*?)\]', dynamic))
if needed != {'libandroid.so', 'libc.so', 'libdl.so'}:
    raise RuntimeError(f'unexpected Android dependencies: {sorted(needed)}')
exported_names = {line.split()[-1] for line in exports.splitlines() if line.strip()}
if exported_names != {'Java_app_aemu_core_VulkanSurface_nativeUpdate'}:
    raise RuntimeError(f'unexpected exported symbols: {sorted(exported_names)}')
for name in ('ANativeWindow_acquire', 'ANativeWindow_fromSurface', 'ANativeWindow_release'):
    if not re.search(r'\bU\s+'+re.escape(name)+r'$', imports, re.MULTILINE):
        raise RuntimeError(f'missing Android native window import: {name}')
print('JNI_ANDROID_ARM64_STRICT_LINK_OK')
print('NEEDED', ', '.join(sorted(needed)))
print('EXPORT Java_app_aemu_core_VulkanSurface_nativeUpdate')
print('OUTPUT', artifact)
