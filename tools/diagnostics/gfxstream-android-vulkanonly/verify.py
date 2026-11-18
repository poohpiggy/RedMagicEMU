#!/usr/bin/env python3
"""Verify fixed Android Vulkan-only window type and expected strict-link block."""
import json
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

here = Path(__file__).resolve().parent
tools, baseline_source, fixed_source, build, gpuinfo, out = (Path(p).resolve(strict=True) for p in sys.argv[1:])
entries = json.loads((build/'compile_commands.json').read_text())
frame = next(e for e in entries if e['file'].endswith('/host/FrameBuffer.cpp'))
if '-DGFXSTREAM_ENABLE_HOST_GLES=0' not in frame['command']:
    raise RuntimeError('build is not Vulkan-only')
compiler = shlex.split(frame['command'])[0]
test = here/'window_type_test.cpp'
def compile_type(source, output):
    return subprocess.run([compiler, '-std=c++17', '-DANDROID', '-I'+str(source/'host'),
                           '-I'+str(source/'include'), '-c', str(test), '-o', str(output)],
                          text=True, capture_output=True)
before = compile_type(baseline_source, out/'window_type_original.o')
if before.returncode == 0 or 'static assertion failed' not in before.stderr:
    raise RuntimeError('baseline type failure not reproduced')
subprocess.run([sys.executable, str(here/'apply_window_type.py'), str(baseline_source/'host/GlesCompat.h'),
                str(out/'GlesCompat.expected.h')], check=True)
if (out/'GlesCompat.expected.h').read_bytes() != (fixed_source/'host/GlesCompat.h').read_bytes():
    raise RuntimeError('fixed source does not match verified patch')
merged = (fixed_source/'host/vulkan/VkCommonOperations.cpp').read_bytes()
if hashlib.sha256(merged).hexdigest() != 'f9d97155698b214d81056faf51f8627ea7da993ecdfb32721e9221d4984d39da':
    raise RuntimeError('merged physical-device checks are absent or changed')
after = compile_type(fixed_source, out/'window_type_fixed.o')
if after.returncode:
    raise RuntimeError(after.stderr)
print('ANDROID_WINDOW_TYPE_RED_GREEN_OK', flush=True)
log = (build.parent/'build-fixed.log').read_text(errors='replace')
errors = set(re.findall(r'undefined symbol: (.+)', log))
if errors != {'getGpuInfoListNative(GpuInfoList*)', 'isVulkanSafeToUseNative()'}:
    raise RuntimeError(f'unexpected prior full-build outcome: {errors}')
print('VULKAN_ONLY_FULL_BUILD_EXPECTED_PLATFORM_SYMBOLS', flush=True)
env = os.environ.copy()
env['LD_LIBRARY_PATH'] = str(tools/'sysroot/usr/lib/x86_64-linux-gnu')
ninja = tools/'sysroot/usr/bin/ninja'
command = subprocess.check_output([str(ninja), '-C', str(build), '-t', 'commands',
                                   'host/libgfxstream_backend.so'], env=env, text=True).splitlines()[-1]
link = shlex.split(command)
if not any('--no-undefined' in arg for arg in link):
    raise RuntimeError('expected strict link')
link[link.index('-o') + 1] = str(out/'libvulkanonly-candidate.so')
link[1:1] = [str(gpuinfo/'query.cpp.o'), str(gpuinfo/'android_loader.cpp.o')]
result = subprocess.run(link, cwd=build, env=env, text=True, capture_output=True)
(out/'strict-link.log').write_text(result.stdout + result.stderr)
errors = set(re.findall(r'undefined symbol: (.+)', result.stderr))
if result.returncode == 0 or errors != {'isVulkanSafeToUseNative()'}:
    raise RuntimeError(f'unexpected strict-link result: {result.returncode} {errors}')
print('VULKAN_ONLY_GPUINFO_CANDIDATE_LINK_EXPECTED_FAILURE isVulkanSafeToUseNative()')
