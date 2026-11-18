#!/usr/bin/env python3
"""Exercise the actual extracted selection and cross-compile its candidate."""
import argparse
import json
import resource
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
START = '    std::vector<uint32_t> deviceScores(physdevCount, 0);'
NEW_START = '#if defined(ANDROID)\n    // The provider'
END = '    sVkEmulation->physdev = physdevs[maxScoringIndex];'

def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, capture_output=True, **kwargs)

def extract(text, start, output):
    a = text.index(start)
    b = text.index(END, a)
    output.write_text(text[a:b])

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path, help='merged d7a VkCommonOperations.cpp')
    parser.add_argument('build', type=Path, help='existing Android build directory')
    parser.add_argument('host_cxx', type=Path)
    parser.add_argument('out', type=Path, help='existing isolated output directory')
    args = parser.parse_args()
    source, build, host_cxx, out = (x.resolve(strict=True) for x in
                                     (args.source, args.build, args.host_cxx, args.out))
    candidate = out / 'VkCommonOperations-identity.cpp'
    run([sys.executable, str(HERE / 'apply_identity.py'), str(source), str(candidate)])
    include = out / 'selection.inc'
    common = [str(host_cxx), '-DANDROID', '-std=c++17', '-I', str(HERE), '-I', str(out),
              str(HERE / 'identity_test.cpp')]
    extract(source.read_text(), START, include)
    baseline = out / 'identity-baseline'
    run(common + ['-o', str(baseline)])
    before = subprocess.run([str(baseline)], capture_output=True, text=True, preexec_fn=no_core)
    if before.returncode == 0 or 'select({a, b}) == 0' not in before.stderr:
        raise RuntimeError('baseline did not demonstrate the wrong identity selection: ' + before.stderr)
    extract(candidate.read_text(), NEW_START, include)
    patched = out / 'identity-patched'
    run(common + ['-o', str(patched)])
    run([str(patched)], preexec_fn=no_core)
    print('IDENTITY_SELECTION_RED_GREEN_OK')

    entries = json.loads((build / 'compile_commands.json').read_text())
    matches = [entry for entry in entries if entry['file'].endswith('/VkCommonOperations.cpp')]
    if len(matches) != 1:
        raise RuntimeError('expected one Android VkCommonOperations build command')
    entry = matches[0]
    argv = shlex.split(entry['command'])
    if '-DANDROID' not in argv or argv[-1] != entry['file']:
        raise RuntimeError('unexpected Android compile command')
    argv[1:1] = ['-I', str(HERE)]
    obj = out / 'VkCommonOperations-identity-android.o'
    for option in ('-o', '-MF', '-MQ'):
        index = argv.index(option)
        argv[index + 1] = str(obj) + ('.d' if option == '-MF' else '')
    argv[-1] = str(candidate)
    run(argv, cwd=entry['directory'], timeout=120)
    nm = Path(argv[0]).parent / 'llvm-nm'
    symbols = run([str(nm), '-C', str(obj)]).stdout
    if 'U gfxstream::vk::getApprovedVulkanIdentity(' not in symbols:
        raise RuntimeError('candidate did not retain required unresolved identity provider')
    readelf = Path(argv[0]).parent / 'llvm-readelf'
    elf = run([str(readelf), '-h', str(obj)]).stdout
    if 'AArch64' not in elf:
        raise RuntimeError('candidate is not Android ARM64')
    print('ANDROID_ARM64_OBJECT_OK IDENTITY_PROVIDER_REQUIRED')

if __name__ == '__main__':
    main()
