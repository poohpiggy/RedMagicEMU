#!/usr/bin/env python3
"""Generate an offline synthetic MP4 and build the standalone Guest probe."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--java-home', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
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
    tools = args.sdk.resolve() / 'build-tools' / '36.0.0'
    jar = args.sdk.resolve() / 'platforms' / 'android-36' / 'android.jar'
    java = args.java_home.resolve()
    env = os.environ.copy()
    env['JAVA_HOME'] = str(java)
    env['PATH'] = str(java / 'bin') + os.pathsep + env.get('PATH', '')

    def exe(name, batch=False):
        suffix = ('.bat' if batch else '.exe') if os.name == 'nt' else ''
        return tools / (name + suffix)

    def run(*command):
        subprocess.run([str(item) for item in command], env=env, check=True)

    with tempfile.TemporaryDirectory(prefix='rme-video-probe-build-') as temp:
        work = Path(temp)
        classes, dex = work / 'classes', work / 'dex'
        classes.mkdir()
        dex.mkdir()
        video, unsigned, aligned = work / 'probe.mp4', work / 'unsigned.apk', work / 'aligned.apk'
        run(args.ffmpeg.resolve(), '-hide_banner', '-loglevel', 'error', '-y',
            '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=24:duration=12',
            '-f', 'lavfi', '-i', 'sine=frequency=800:sample_rate=48000:duration=12',
            '-c:v', 'libx264', '-profile:v', 'baseline', '-pix_fmt', 'yuv420p',
            '-c:a', 'aac', '-b:a', '96k', '-movflags', '+faststart', '-shortest', video)
        run(exe('aapt'), 'package', '-f', '-M', src / 'AndroidManifest.xml', '-I', jar, '-F', unsigned)
        run(java / 'bin' / ('javac.exe' if os.name == 'nt' else 'javac'), '--release', '8',
            '-Xlint:-options', '-classpath', jar, '-d', classes,
            src / 'src' / 'app' / 'rme' / 'probe' / 'VideoActivity.java')
        run(exe('d8', batch=True), '--min-api', '26', '--lib', jar, '--output', dex,
            *sorted((classes / 'app' / 'rme' / 'probe').glob('*.class')))
        with zipfile.ZipFile(unsigned, 'a') as apk:
            apk.write(dex / 'classes.dex', 'classes.dex', compress_type=zipfile.ZIP_STORED)
            apk.write(video, 'assets/probe.mp4', compress_type=zipfile.ZIP_STORED)
        run(exe('zipalign'), '-P', '16', '-f', '4', unsigned, aligned)
        run(exe('apksigner', batch=True), 'sign', '--ks', args.keystore.resolve(),
            '--ks-key-alias', args.key_alias, '--ks-pass', 'env:RME_PROBE_STORE_PASS',
            '--key-pass', 'env:RME_PROBE_KEY_PASS', '--v4-signing-enabled', 'false', '--out', output, aligned)
    print('Built:', output)


if __name__ == '__main__':
    main()
