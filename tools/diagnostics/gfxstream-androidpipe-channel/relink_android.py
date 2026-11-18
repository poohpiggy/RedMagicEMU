#!/usr/bin/env python3
"""Strict-link the isolated real Android components; no device execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--without-uuid', action='store_true',
                    help='use the verified merged GPU-selection object without the optional UUID experiment')
parser.add_argument('--gpuinfo-dir', type=Path,
                    help='include the isolated GPU information candidate objects, not a safety provider')
parser.add_argument('--android-wsi-dir', type=Path,
                    help='use Android WSI candidate objects built from the fixed merged sources')
parser.add_argument('--headless-dir', type=Path,
                    help='use the guarded Android headless configuration candidate')
parser.add_argument('gfxstream_root', type=Path)
parser.add_argument('aemu_build', type=Path)
parser.add_argument('composer_root', type=Path)
parser.add_argument('identity_object', type=Path)
parser.add_argument('output_root', type=Path)
args = parser.parse_args()
root, aemu, composer, identity, output_root = (
    x.resolve(strict=True) for x in
    (args.gfxstream_root, args.aemu_build, args.composer_root, args.identity_object, args.output_root))
out = Path(tempfile.mkdtemp(prefix='rme-integrated-link-', dir=output_root))
env = os.environ.copy()
env['LD_LIBRARY_PATH'] = str(root / 'sysroot/usr/lib/x86_64-linux-gnu')
ninja = root / 'sysroot/usr/bin/ninja'

def run(argv, cwd=None, check=True):
    return subprocess.run([str(x) for x in argv], cwd=cwd, env=env, text=True,
                          capture_output=True, check=check, timeout=120)

def ninja_command(target):
    lines = run([ninja, '-C', aemu, '-t', 'commands', target]).stdout.splitlines()
    return lines[-1]

def compile_copy(command, source, output, extra_include):
    argv = shlex.split(command)
    if '-DANDROID' not in argv or '-c' not in argv:
        raise RuntimeError('not an Android compile command')
    argv[1:1] = ['-I', str(extra_include)]
    for option in ('-o', '-MF', '-MT', '-MQ'):
        if option in argv:
            i = argv.index(option)
            argv[i + 1] = str(output) + ('.d' if option == '-MF' else '')
    argv[argv.index('-c') + 1] = str(source)
    run(argv, cwd=aemu if 'aemu/host-common/CMakeFiles' in command else composer / 'build-composer')
    return output

android_pipe = out / 'AndroidPipe.cpp'
run([sys.executable, HERE / 'waker_patch.py', root / 'aemu/host-common/AndroidPipe.cpp', android_pipe])
compile_cmd = ninja_command('aemu/host-common/CMakeFiles/aemu-host-common.dir/AndroidPipe.cpp.o')
pipe_obj = compile_copy(compile_cmd, android_pipe, out / 'AndroidPipe.o', HERE)
vm_obj = compile_copy(compile_cmd, root / 'aemu/host-common/VmLock.cpp', out / 'VmLock.o', HERE)
link_cmd = ninja_command('aemu/host-common/libaemu-host-common.so')
if not link_cmd.startswith(': && ') or not link_cmd.endswith(' && :'):
    raise RuntimeError('unexpected aemu link command')
argv = shlex.split(link_cmd[len(': && '):-len(' && :')])
old_pipe = 'aemu/host-common/CMakeFiles/aemu-host-common.dir/AndroidPipe.cpp.o'
if argv.count(old_pipe) != 1 or not any('--no-undefined' in x for x in argv):
    raise RuntimeError('unexpected aemu link inputs')
argv[argv.index(old_pipe)] = str(pipe_obj)
argv.insert(1, str(vm_obj))
out_aemu = out / 'libaemu-host-common.so'
argv[argv.index('-o') + 1] = str(out_aemu)
for i, arg in enumerate(argv):
    if arg.startswith('--dependency-file='):
        argv[i] = '--dependency-file=' + str(out / 'aemu-link.d')
run(argv, cwd=aemu)
nm = Path(argv[0]).parent / 'llvm-nm'
symbols = run([nm, '-D', '-C', out_aemu]).stdout
if 'android::rmeInitPipeWaker(' not in symbols or 'android::VmLock::set(' not in symbols:
    raise RuntimeError('rebuilt aemu does not export the required real functions')
print('ANDROID_AEMU_STRICT_LINK_OK', flush=True)

build = composer / 'build-composer'
entries = json.loads((build / 'compile_commands.json').read_text())
frame = [e for e in entries if e['file'].endswith('/FrameBuffer.cpp')]
if len(frame) != 1:
    raise RuntimeError('expected one FrameBuffer compile command')
objects = []
for name in ('channel', 'socket_session', 'socket_server', 'primary_display', 'vm_snapshot',
             'renderer_start', 'renderer_bootstrap', 'framebuffer_color_registry'):
    objects.append(compile_copy(frame[0]['command'], HERE / (name + '.cpp'), out / (name + '.o'), HERE))
print('ANDROID_REAL_ADAPTER_OBJECTS_OK', flush=True)

log = (composer / 'build-composer.log').read_text()
links = [line for line in log.splitlines() if line.startswith('/var/tmp/rme-ndk') and
         ' -o host/libgfxstream_backend.so ' in line]
if len(links) != 1:
    raise RuntimeError('expected one recorded strict backend link command')
link = shlex.split(links[0])
if not any('--no-undefined' in x for x in link):
    raise RuntimeError('backend link is not strict')
ar = Path(link[0]).parent / 'llvm-ar'
original_archive = build / 'host/vulkan/liblib_vulkan_server.a'
members = run([ar, 't', original_archive]).stdout.splitlines()
old = [m for m in members if m.endswith('/VkCommonOperations.cpp.o')]
if len(old) != 1 or not all(Path(m).is_file() for m in members):
    raise RuntimeError('unexpected thin archive members')
nm_identity = run([nm, '-C', identity]).stdout
has_identity_provider = 'U gfxstream::vk::getApprovedVulkanIdentity(' in nm_identity
if args.without_uuid:
    # Pin the exact merged object: it retains the existing GPU type, API and
    # combined graphics/compute queue checks. The composer's original object
    # is not equivalent and must not silently replace it.
    expected_sha = '9e4afadc8ae7b5a5e082a2fb625e86c7bad67ef16d7dc70be71c2681924b4f2a'
    if has_identity_provider or hashlib.sha256(identity.read_bytes()).hexdigest() != expected_sha:
        raise RuntimeError('expected the verified merged GPU-selection object without UUID experiment')
else:
    if not has_identity_provider:
        raise RuntimeError('identity object does not require the approved provider')
candidate_archive = out / 'libvulkan-candidate.a'
replacement = {old[0]: identity}
if args.android_wsi_dir:
    if not args.without_uuid:
        raise RuntimeError('Android WSI candidate is based on the merged non-UUID source')
    wsi = args.android_wsi_dir.resolve(strict=True)
    display = [m for m in members if m.endswith('/DisplaySurfaceVk.cpp.o')]
    if len(display) != 1:
        raise RuntimeError('expected exactly one DisplaySurfaceVk object')
    replacement[old[0]] = (wsi / 'VkCommonOperations-android.o').resolve(strict=True)
    replacement[display[0]] = (wsi / 'DisplaySurfaceVk-android.o').resolve(strict=True)
run([ar, 'crsT', candidate_archive] + [replacement.get(m, Path(m)) for m in members])
archive_arg = 'host/vulkan/liblib_vulkan_server.a'
matches = [i for i, value in enumerate(link) if value.endswith('/libaemu-host-common.so')]
if link.count(archive_arg) != 1 or len(matches) != 1:
    raise RuntimeError('unexpected backend link dependencies')
link[link.index(archive_arg)] = str(candidate_archive)
if args.headless_dir:
    headless_object = (args.headless_dir.resolve(strict=True) / 'opengles-headless.o').resolve(strict=True)
    if ' T rme_set_renderer_headless' not in run([nm, '-C', headless_object]).stdout:
        raise RuntimeError('headless configuration symbol missing')
    common_arg = 'host/gl/gl-host-common/libgl_host_common.a'
    common_members = run([ar, 't', build / common_arg]).stdout.splitlines()
    old_common = [m for m in common_members if m.endswith('/opengles.cpp.o')]
    if link.count(common_arg) != 1 or len(old_common) != 1 or not all(Path(m).is_file() for m in common_members):
        raise RuntimeError('unexpected gl-host-common archive')
    common_archive = out / 'libgl-host-common-candidate.a'
    run([ar, 'crsT', common_archive] + [headless_object if m == old_common[0] else Path(m) for m in common_members])
    link[link.index(common_arg)] = str(common_archive)
link[matches[0]] = str(out_aemu)
if args.android_wsi_dir:
    framebuffer = [i for i, value in enumerate(link) if value.endswith('/FrameBuffer.cpp.o')]
    if len(framebuffer) != 1:
        raise RuntimeError('expected exactly one FrameBuffer link object')
    link[framebuffer[0]] = str((wsi / 'FrameBuffer-android.o').resolve(strict=True))
link[link.index('-o') + 1] = str(out / 'libgfxstream-candidate.so')
if args.gpuinfo_dir:
    gpuinfo_dir = args.gpuinfo_dir.resolve(strict=True)
    gpuinfo_objects = [gpuinfo_dir / 'query.cpp.o', gpuinfo_dir / 'android_loader.cpp.o']
    symbols = run([nm, '-C'] + gpuinfo_objects).stdout
    if ' T getGpuInfoListNative(GpuInfoList*)' not in symbols or ' T isVulkanSafeToUseNative()' in symbols:
        raise RuntimeError('unexpected GPU information candidate symbols')
    objects.extend(gpuinfo_objects)
link[1:1] = [str(x) for x in objects]
result = run(link, cwd=build, check=False)
errors = sorted(set(re.findall(r'undefined symbol: (.+)', result.stderr)))
(out / 'strict-link.log').write_text(result.stdout + result.stderr)
print('STRICT_LINK_EXIT', result.returncode)
for error in errors:
    print('UNRESOLVED', error)
print('OUTPUT', out)
if result.returncode == 0:
    raise RuntimeError('unexpected successful link: verify providers and safety checks')
if not errors:
    raise RuntimeError('link failed for a reason other than unresolved symbols; inspect log')
expected = {
    'isVulkanSafeToUseNative()',
}
if not args.gpuinfo_dir:
    expected.add('getGpuInfoListNative(GpuInfoList*)')
    expected.add('rme::nativeGpuInfoResultFor(GpuInfoList const*)')
if not args.headless_dir:
    expected.add('rme_set_renderer_headless')
if not args.without_uuid:
    expected.add('gfxstream::vk::getApprovedVulkanIdentity(gfxstream::vk::ApprovedVulkanIdentity*)')
if set(errors) != expected:
    raise RuntimeError('unexpected unresolved-symbol set; inspect log')
print('EXPECTED_BACKEND_STRICT_LINK_FAILURE')
