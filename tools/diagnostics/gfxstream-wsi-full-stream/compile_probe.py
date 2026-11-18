#!/usr/bin/env python3
"""Compile full generated host/guest sources for an isolated host stream probe."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('build_root', type=Path)
p.add_argument('generated', type=Path)
p.add_argument('state_dir', type=Path)
p.add_argument('output', type=Path, help='new directory')
a = p.parse_args()
root = a.build_root.resolve(strict=True)
gen = a.generated.resolve(strict=True)
state = a.state_dir.resolve(strict=True)
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[3]

def compile_one(database, suffix, target, output, includes, extra):
    entries = [x for x in json.loads(database.read_text()) if x['file'].endswith(suffix)]
    if len(entries) != 1:
        raise SystemExit(f'expected one compile command for {suffix}')
    entry = entries[0]
    cmd = list(entry.get('arguments') or shlex.split(entry['command']))
    cmd[1:1] = ['-I' + str(x) for x in includes] + extra
    cmd[cmd.index(entry['file'])] = str(target)
    cmd[cmd.index('-o') + 1] = str(output)
    if '-MF' in cmd:
        cmd[cmd.index('-MF') + 1] = str(output) + '.d'
    if '-MQ' in cmd:
        cmd[cmd.index('-MQ') + 1] = str(output)
    output.with_suffix('.command.txt').write_text(shlex.join(cmd) + '\n')
    result = subprocess.run(cmd, cwd=entry['directory'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output.with_suffix('.log').write_text(result.stdout)
    print(target.name, 'OK' if result.returncode == 0 else 'FAIL', flush=True)
    if result.returncode:
        print(result.stdout[-6000:])
        raise SystemExit(result.returncode)

host_includes = [state, gen / 'host/cereal/common', gen / 'host/cereal', gen / 'host',
                 repo / 'tools/diagnostics/gfxstream-surface-wire',
                 repo / 'tools/diagnostics/gfxstream-android-surface/test_include',
                 repo / 'native/vulkanbridge']
compile_one(root / 'build-host/compile_commands.json', '/VkDecoder.cpp', gen / 'host/VkDecoder.cpp',
            out / 'VkDecoder.o', host_includes, ['-DVK_USE_PLATFORM_ANDROID_KHR', '-include', 'vulkan/vulkan.h'])
support_names = ('VkDecoderGlobalState', 'VkDecoderSnapshot', 'goldfish_vk_extension_structs',
                 'goldfish_vk_marshaling', 'goldfish_vk_reserved_marshaling',
                 'goldfish_vk_deepcopy', 'goldfish_vk_dispatch', 'goldfish_vk_transform')
state_copy = out / 'state'
state_copy.mkdir()
for filename in ('VkDecoderGlobalState.cpp', 'VkDecoderGlobalState.h'):
    shutil.copy2(state / filename, state_copy / filename)
def compile_support(name):
    filename = name + '.cpp'
    target = state_copy / filename if name == 'VkDecoderGlobalState' else (
        gen / 'host' / filename if name == 'VkDecoderSnapshot' else gen / 'host/cereal/common' / filename)
    includes = [state_copy] + [x for x in host_includes if x != state]
    compile_one(root / 'build-host/compile_commands.json', '/' + filename, target,
                out / (name + '.o'), includes, ['-DVK_USE_PLATFORM_ANDROID_KHR', '-include', 'vulkan/vulkan.h'])
with ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(compile_support, support_names))

cc = root / 'bin/cc'
if not cc.exists():
    cc = Path('/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang')
stub = out / 'native_window_stub.c'
stub.write_text('''#include <android/native_window_jni.h>
#include <stdint.h>
void ANativeWindow_acquire(ANativeWindow* p) { (void)p; }
void ANativeWindow_release(ANativeWindow* p) { (void)p; }
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* e, jobject o) { (void)e; (void)o; return (ANativeWindow*)(uintptr_t)1; }
''')
for name, target in (('native_window_stub', stub),
                     ('host_guest_surface', repo / 'tools/diagnostics/gfxstream-surface-wire/host_guest_surface.c'),
                     ('host_surface', repo / 'native/vulkanbridge/host_surface.c'),
                     ('surface_jni', repo / 'native/vulkanbridge/surface_jni.c')):
    cmd = [str(cc), '-std=c11', '-Wall', '-Wextra', '-Werror', '-DVK_USE_PLATFORM_ANDROID_KHR',
           '-I' + str(repo / 'tools/diagnostics/gfxstream-android-surface/test_include'),
           '-I' + str(root / 'src/common/vulkan/include'), '-I' + str(repo / 'native/vulkanbridge'),
           '-c', str(target), '-o', str(out / (name + '.o'))]
    subprocess.run(cmd, check=True)
encoder = out / 'guest/vulkan_enc'
shutil.copytree(root / 'src/guest/vulkan_enc', encoder)
for candidate in (gen / 'guest').glob('*'):
    if candidate.suffix in ('.cpp', '.h') and candidate.name.startswith(('VkEncoder', 'goldfish_vk_')):
        shutil.copy2(candidate, encoder / candidate.name)
for name in ('egl', 'OpenglSystemCommon'):
    (encoder.parent / name).symlink_to(root / 'src/guest' / name, target_is_directory=True)
guest_includes = [encoder]
for name in ('VkEncoder', 'goldfish_vk_counting_guest', 'goldfish_vk_deepcopy_guest',
             'goldfish_vk_extension_structs_guest', 'goldfish_vk_marshaling_guest',
             'goldfish_vk_reserved_marshaling_guest', 'goldfish_vk_transform_guest'):
    compile_one(root / 'build-guest/compile_commands.json', '/vulkan_enc/VkEncoder.cpp',
                encoder / (name + '.cpp'), out / (name + '.o'), guest_includes,
                ['-include', 'vulkan/vulkan.h'])
print('FULL_HOST_AND_GUEST_OBJECTS_COMPILED', out)

# A strict link probe deliberately uses only existing complete CPU libraries.
# Its output is diagnostic: an executable, if produced, is not run here.
main = out / 'main.cpp'
main.write_text('int main() { return 0; }\n')
host_objs = [out / 'VkDecoder.o'] + [out / (name + '.o') for name in support_names] + [out / (name + '.o') for name in
        ('native_window_stub', 'host_guest_surface', 'host_surface', 'surface_jni')]
guest_objs = [out / (name + '.o') for name in ('VkEncoder', 'goldfish_vk_counting_guest',
        'goldfish_vk_deepcopy_guest', 'goldfish_vk_extension_structs_guest', 'goldfish_vk_marshaling_guest',
        'goldfish_vk_reserved_marshaling_guest', 'goldfish_vk_transform_guest')]
for name, objects, libraries in (
    ('host', host_objs, [str(root / 'build-host/host/vulkan/liblib_vulkan_server.a'),
                         '-lgfxstream_backend', '-laemu-host-common', '-laemu-base', '-laemu-logging', '-laemu-snapshot']),
    ('guest', guest_objs, [str(root / 'build-guest/guest/vulkan/libvulkan_gfxstream.so')]),
):
    link = [str(root / 'bin/cxx'), str(main), *(str(x) for x in objects),
            '-L' + str(root / 'prefix/lib'), '-Wl,-rpath,' + str(root / 'prefix/lib'),
            *libraries, '-pthread', '-Wl,--no-undefined', '-o', str(out / (name + '-probe'))]
    (out / (name + '-link.command.txt')).write_text(shlex.join(link) + '\n')
    result = subprocess.run(link, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / (name + '-link.log')).write_text(result.stdout)
    print('FULL_' + name.upper() + '_LINK_' + ('OK' if result.returncode == 0 else 'FAILED'), flush=True)
