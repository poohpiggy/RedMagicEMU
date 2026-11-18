#!/usr/bin/env python3
"""Build patched fixed sources and exercise Android WSI with synthetic dispatch."""
import json
import pathlib
import shlex
import subprocess
import sys

here = pathlib.Path(__file__).resolve().parent
root, build, merged, out = (pathlib.Path(p).resolve() for p in sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
surface = root / 'src/host/vulkan/DisplaySurfaceVk.cpp'
subprocess.run([sys.executable, str(here/'apply.py'), str(surface), str(merged), str(out)], check=True)
framebuffer = root / 'src/host/FrameBuffer.cpp'
failure_include = out/'framebuffer_failure.inc'
def run_failure_test(expect_success):
    subprocess.run([str(root/'bin/cxx'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(out),
                    str(here/'framebuffer_failure_test.cpp'), '-o', str(out/'framebuffer_failure_test')], check=True)
    result = subprocess.run([str(out/'framebuffer_failure_test')])
    if (result.returncode == 0) != expect_success:
        raise RuntimeError('unexpected FrameBuffer failure-path result')
subprocess.run([sys.executable, str(here/'extract_failure.py'), str(framebuffer), str(failure_include)], check=True)
run_failure_test(False)
subprocess.run([sys.executable, str(here/'apply_framebuffer.py'), str(framebuffer), str(out/'FrameBuffer.cpp')], check=True)
subprocess.run([sys.executable, str(here/'extract_failure.py'), str(out/'FrameBuffer.cpp'), str(failure_include)], check=True)
run_failure_test(True)
subprocess.run([sys.executable, str(here/'requirements_test.py'), str(out/'DisplaySurfaceVk.cpp'), str(out/'VkCommonOperations.cpp')], check=True)
common = (out/'VkCommonOperations.cpp').read_text()
start = common.index('static bool extensionsSupported(')
end = common.index('\n}\n', start) + 3
(out/'extracted_extensions.inc').write_text(common[start:end])
start = common.index('    std::vector<const char*> androidSurfaceInstanceExtNames = {')
end = common.index('\n    };', start) + len('\n    };')
(out/'extracted_requirements.inc').write_text(common[start:end])
subprocess.run([str(root/'bin/cxx'), '-std=c++17', '-DANDROID', '-I'+str(out),
                '-I'+str(root/'src/common/vulkan/include'), str(here/'extensions_test.cpp'),
                '-o', str(out/'extensions_test')], check=True)
subprocess.run([str(out/'extensions_test')], check=True)

def compile_from(builddir, basename, target, extra=()):
    entries = json.loads((builddir/'compile_commands.json').read_text())
    entry = next(e for e in entries if e['file'].endswith('/'+basename))
    args = shlex.split(entry['command'])
    cleaned = []
    i = 0
    while i < len(args):
        if args[i] in ('-o', '-MF', '-MQ', '-c'):
            i += 2
        elif args[i] == '-MD':
            i += 1
        else:
            cleaned.append(args[i]); i += 1
    cmd = cleaned + list(extra) + ['-c', str(out/basename), '-o', str(target)]
    subprocess.run(cmd, cwd=entry['directory'], check=True)

compile_from(build, 'DisplaySurfaceVk.cpp', out/'DisplaySurfaceVk-android.o')
compile_from(build, 'VkCommonOperations.cpp', out/'VkCommonOperations-android.o')
compile_from(build, 'FrameBuffer.cpp', out/'FrameBuffer-android.o')
cutils = '-I/var/tmp/rme-mesa-android-6ekppv9b/aosp-core/libcutils/include'
compile_from(root/'build-host', 'DisplaySurfaceVk.cpp', out/'DisplaySurfaceVk-test.o',
             ['-DANDROID', '-I'+str(here/'test_include'), cutils])
host_entries = json.loads((root/'build-host/compile_commands.json').read_text())
host_entry = next(e for e in host_entries if e['file'].endswith('/DisplaySurfaceVk.cpp'))
cmd = shlex.split(host_entry['command'])
cleaned = []
i = 0
while i < len(cmd):
    if cmd[i] in ('-o', '-MF', '-MQ', '-c'):
        i += 2
    elif cmd[i] == '-MD':
        i += 1
    else:
        cleaned.append(cmd[i]); i += 1
library = root/'prefix/lib'
subprocess.run(cleaned + ['-DANDROID', '-I'+str(here/'test_include'), cutils, str(here/'surface_test.cpp'),
               str(out/'DisplaySurfaceVk-test.o'), '-L'+str(library), '-Wl,-rpath,'+str(library),
               '-lgfxstream_backend', '-laemu-host-common', '-laemu-base', '-laemu-logging', '-pthread',
               '-o', str(out/'surface_test')], cwd=host_entry['directory'], check=True)
subprocess.run([str(out/'surface_test')], check=True)
print('ANDROID_WSI_AARCH64_OBJECTS_OK')
