#!/usr/bin/env python3
"""Build the standalone Guest AHardwareBuffer diagnostic."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--keystore', type=Path, required=True)
    parser.add_argument('--key-alias', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get('RME_PROBE_STORE_PASS') or not os.environ.get('RME_PROBE_KEY_PASS'):
        parser.error('set RME_PROBE_STORE_PASS and RME_PROBE_KEY_PASS in the environment')
    output = args.output.resolve()
    if output.exists():
        parser.error('output already exists; choose another path')
    src = Path(__file__).resolve().parent
    sdk = args.sdk.resolve()
    tools = sdk / 'build-tools' / '36.0.0'
    jar = sdk / 'platforms' / 'android-36' / 'android.jar'
    java = args.java_home.resolve()
    host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64'
    compiler = args.ndk.resolve() / 'toolchains' / 'llvm' / 'prebuilt' / host / 'bin' / ('clang.exe' if os.name == 'nt' else 'clang')
    env = os.environ.copy()
    env['JAVA_HOME'] = str(java)
    env['PATH'] = str(java / 'bin') + os.pathsep + env.get('PATH', '')

    def executable(name, batch=False):
        suffix = ('.bat' if batch else '.exe') if os.name == 'nt' else ''
        return tools / (name + suffix)

    def run(*command):
        subprocess.run([str(item) for item in command], env=env, check=True)

    with tempfile.TemporaryDirectory(prefix='rme-ahb-formats-build-') as temp:
        work = Path(temp)
        classes, dex = work / 'classes', work / 'dex'
        classes.mkdir()
        dex.mkdir()
        library, unsigned, aligned = work / 'librme_ahbprobe.so', work / 'unsigned.apk', work / 'aligned.apk'
        run(compiler, '--target=aarch64-linux-android29', '-O2', '-fPIC', '-shared',
            '-Wl,--no-undefined', '-o', library, src / 'probe.c', '-landroid', '-llog')
        run(executable('aapt'), 'package', '-f', '-M', src / 'AndroidManifest.xml', '-I', jar, '-F', unsigned)
        javac = java / 'bin' / ('javac.exe' if os.name == 'nt' else 'javac')
        run(javac, '--release', '8', '-Xlint:-options', '-classpath', jar,
            '-d', classes, src / 'src' / 'app' / 'rme' / 'probe' / 'BufferActivity.java')
        run(executable('d8', batch=True), '--min-api', '29', '--lib', jar,
            '--output', dex, *sorted((classes / 'app' / 'rme' / 'probe').glob('*.class')))
        with zipfile.ZipFile(unsigned, 'a') as apk:
            apk.write(dex / 'classes.dex', 'classes.dex', compress_type=zipfile.ZIP_STORED)
            apk.write(library, 'lib/arm64-v8a/librme_ahbprobe.so', compress_type=zipfile.ZIP_STORED)
        run(executable('zipalign'), '-P', '16', '-f', '4', unsigned, aligned)
        run(executable('apksigner', batch=True), 'sign', '--ks', args.keystore.resolve(),
            '--ks-key-alias', args.key_alias, '--ks-pass', 'env:RME_PROBE_STORE_PASS',
            '--key-pass', 'env:RME_PROBE_KEY_PASS', '--v4-signing-enabled', 'false',
            '--out', output, aligned)
    print('Built:', output)


if __name__ == '__main__':
    main()
