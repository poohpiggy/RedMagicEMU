#!/usr/bin/env python3
"""Strict-link one isolated Vulkan-only backend with the paired WSI candidate.

The optional Android preflight binds the renderer to the device and driver
that complete the fixed compute probe. This script never runs the backend.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('tools', type=Path, help='pinned gfxstream tools root')
parser.add_argument('build', type=Path, help='existing Vulkan-only Meson build')
parser.add_argument('decoder', type=Path, help='paired VkDecoder.o built with the Vulkan-only build flags')
parser.add_argument('components', type=Path, help='paired state, snapshot and cereal objects built with the Vulkan-only flags')
parser.add_argument('surface_adapter', type=Path, help='compiled EMU-owned surface adapter objects')
parser.add_argument('gpuinfo', type=Path, help='existing actual Vulkan GPU-info query candidate')
parser.add_argument('output', type=Path, help='new output directory')
parser.add_argument('--render-thread', type=Path,
                    help='optional Vulkan-only RC/WSI-negotiated RenderThread object')
parser.add_argument('--with-android-preflight', action='store_true',
                    help='build the compute preflight and match its device/driver UUIDs')
parser.add_argument('--androidpipe-host-common', type=Path,
                    help='link the compute-only socket binding with the matching AndroidPipe waker library')
args = parser.parse_args()
if args.androidpipe_host_common and (args.render_thread or not args.with_android_preflight):
    parser.error('--androidpipe-host-common requires the preflight and builds its own RenderThread')
tools, build, decoder, components, adapter, gpuinfo = (
    p.resolve(strict=True) for p in (args.tools, args.build, args.decoder,
                                      args.components, args.surface_adapter, args.gpuinfo))
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, LD_LIBRARY_PATH=str(tools / 'sysroot/usr/lib/x86_64-linux-gnu'))
ninja = tools / 'sysroot/usr/bin/ninja'

def run(command, **kwargs):
    return subprocess.run([str(part) for part in command], cwd=build, env=env,
                          text=True, capture_output=True, **kwargs)

command = run([ninja, '-C', build, '-t', 'commands', 'host/libgfxstream_backend.so'], check=True).stdout.splitlines()[-1]
link = shlex.split(command)
if not any('--no-undefined' in x for x in link):
    raise RuntimeError('backend is not configured for strict linking')
compiler_dir = Path(link[0]).parent
ar = compiler_dir / 'llvm-ar'
nm = compiler_dir / 'llvm-nm'
source = build.parent / 'source/host/vulkan/VkCommonOperations.cpp'
if hashlib.sha256(source.read_bytes()).hexdigest() != 'f9d97155698b214d81056faf51f8627ea7da993ecdfb32721e9221d4984d39da':
    raise RuntimeError('original merged GPU selection checks missing or changed')

def replace_archive(original, replacements, output):
    members = run([ar, 't', original], check=True).stdout.splitlines()
    replacements = dict(replacements)
    for name in replacements:
        matches = [member for member in members
                   if Path(member).name in (name, 'common_' + name)]
        if len(matches) != 1:
            raise RuntimeError(f'expected exactly one archive member for {name}: {matches}')
    result = []
    for member in members:
        suffix = Path(member).name
        matches = [key for key in replacements if suffix == key or suffix == 'common_' + key]
        if len(matches) > 1:
            raise RuntimeError('ambiguous archive member: ' + member)
        if matches:
            result.append(replacements.pop(matches[0]).resolve(strict=True))
        else:
            result.append((build / member).resolve(strict=True))
    if replacements:
        raise RuntimeError('archive members not found: ' + ', '.join(replacements))
    run([ar, 'crsT', output, *result], check=True)
    return output

server = 'host/vulkan/liblib_vulkan_server.a'
cereal = 'host/vulkan/cereal/libcereal.a'
if link.count(server) != 1 or link.count(cereal) != 1:
    raise RuntimeError('unexpected Vulkan/cereal archive inputs')
server_replacements = {'VkDecoder.cpp.o': decoder,
    'VkDecoderGlobalState.cpp.o': components / 'VkDecoderGlobalState.cpp.o',
    'VkDecoderSnapshot.cpp.o': components / 'VkDecoderSnapshot.cpp.o'}
preflight_objects = []
if args.with_android_preflight:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gfxstream-android-gpuinfo'))
    from verify_preflight import build_preflight

    selection, preflight_objects = build_preflight(tools, build, out / 'preflight')
    server_replacements['VkCommonOperations.cpp.o'] = selection
    link.insert(link.index('-Wl,--end-group'), '-lvulkan')
cereal_names = ['goldfish_vk_extension_structs.cpp', 'goldfish_vk_marshaling.cpp',
                'goldfish_vk_reserved_marshaling.cpp', 'goldfish_vk_deepcopy.cpp',
                'goldfish_vk_dispatch.cpp', 'goldfish_vk_transform.cpp']
cereal_replacements = {name + '.o': components / (name + '.o') for name in cereal_names}
compile_records = [decoder.parent / 'command.txt']
compile_records += [components / (name + '.command.txt') for name in
                    ['VkDecoderGlobalState.cpp', 'VkDecoderSnapshot.cpp', *cereal_names]]
socket_objects = []
if args.androidpipe_host_common:
    host_common = args.androidpipe_host_common.resolve(strict=True)
    exports = run([nm, '-D', '-C', host_common], check=True).stdout
    if ' T android::rmeInitPipeWaker(' not in exports or ' T android::VmLock::set(' not in exports:
        raise RuntimeError('host-common must include the matching waker and VM lock')
    bound_dir = out / 'process-pipe'
    compiler = Path(__file__).resolve().parents[1] / 'gfxstream-vulkanonly-rc/compile_renderthread.py'
    subprocess.run([sys.executable, compiler, build.parent / 'source',
                    build / 'compile_commands.json', bound_dir, '--bind-process-pipe'],
                   check=True, timeout=120)
    for name in ('RenderThread', 'RenderChannelImpl', 'RendererImpl', 'RenderLibImpl', 'RenderWindow'):
        index, = [i for i, arg in enumerate(link) if arg.endswith('/' + name + '.cpp.o')]
        link[index] = str(bound_dir / (name + '.o'))
    index, = [i for i, arg in enumerate(link) if arg.endswith('/libaemu-host-common.so')]
    link[index] = str(host_common)
    socket_objects = [bound_dir / (name + '.o') for name in
                      ('channel', 'socket_session', 'socket_server', 'principal_scope')]
if args.render_thread:
    render_thread = args.render_thread.resolve(strict=True)
    compile_records.append(render_thread.parent / 'command.txt')
    matches = [i for i, arg in enumerate(link) if arg.endswith('/RenderThread.cpp.o')]
    if len(matches) != 1:
        raise RuntimeError('expected exactly one RenderThread link object')
    symbols = run([nm, '-C', render_thread], check=True).stdout
    if ' U gfxstream::vk::rmeWsiProtocolCompiled()' not in symbols:
        raise RuntimeError('render thread was not compiled with negotiated WSI')
    link[matches[0]] = str(render_thread)
for record in compile_records:
    if '-DGFXSTREAM_ENABLE_HOST_GLES=0' not in record.read_text():
        raise RuntimeError('candidate object was not built with Vulkan-only flags: ' + str(record))
link[link.index(server)] = str(replace_archive(server, server_replacements, out / 'vulkan-candidate.a'))
link[link.index(cereal)] = str(replace_archive(cereal, cereal_replacements, out / 'cereal-candidate.a'))
gpu_objects = [(gpuinfo / name).resolve(strict=True) for name in ('query.cpp.o', 'android_loader.cpp.o')]
gpu_symbols = run([nm, '-C', *gpu_objects], check=True).stdout
if ' T getGpuInfoListNative(GpuInfoList*)' not in gpu_symbols or ' T isVulkanSafeToUseNative()' in gpu_symbols:
    raise RuntimeError('unexpected GPU provider symbols')
surface_objects = [(adapter / name).resolve(strict=True) for name in
                   ('host_guest_surface_android.o', 'host_surface-android-cpp-link.o',
                    'surface_jni-android-cpp-link.o')]
link[1:1] = [str(p) for p in gpu_objects + surface_objects + preflight_objects + socket_objects]
if args.with_android_preflight:
    link = [arg for arg in link if not arg.startswith('-Wl,-rpath,')]
link[link.index('-o') + 1] = str(out / 'libgfxstream-wsi-candidate.so')
(out / 'link-command.txt').write_text(shlex.join(link) + '\n')
result = run(link)
(out / 'strict-link.log').write_text(result.stdout + result.stderr)
errors = sorted(set(re.findall(r'undefined symbol: (.+)', result.stderr)))
print('STRICT_LINK_EXIT', result.returncode)
for error in errors:
    print('UNRESOLVED', error)
print('OUTPUT', out)
if args.with_android_preflight:
    if result.returncode != 0:
        raise RuntimeError('strict link failed; inspect strict-link.log')
    artifact = out / 'libgfxstream-wsi-candidate.so'
    elf = run([compiler_dir / 'llvm-readelf', '-h', '-l', '-d', artifact], check=True).stdout
    (out / 'backend-elf.txt').write_text(elf)
    loads = [line.split()[-1] for line in elf.splitlines() if line.lstrip().startswith('LOAD ')]
    if ('AArch64' not in elf or 'Shared library: [libvulkan.so]' not in elf
            or not loads or any(int(align, 16) != 16384 for align in loads)):
        raise RuntimeError('unexpected Android ARM64 backend layout or Vulkan dependency')
    print('ANDROID_ARM64_BACKEND_STRICT_LINK_OK')
    print('SHA256', hashlib.sha256(artifact.read_bytes()).hexdigest())
    sys.exit(0)
if result.returncode == 0:
    raise RuntimeError('unexpected success; independently verify the real safety provider')
if errors != ['isVulkanSafeToUseNative()']:
    raise RuntimeError('unexpected strict-link failure; inspect strict-link.log')
print('EXPECTED_REAL_SAFETY_PROVIDER_MISSING')
