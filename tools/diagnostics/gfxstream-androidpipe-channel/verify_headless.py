#!/usr/bin/env python3
import json
from pathlib import Path
import shlex
import subprocess
import sys

here = Path(__file__).resolve().parent
root, build, out = (Path(p).resolve() for p in sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
source = root/'src/host/gl/gl-host-common/opengles.cpp'
patched = out/'opengles-headless.cpp'
subprocess.run([sys.executable, str(here/'apply_headless.py'), str(source), str(patched)], check=True)
text = patched.read_text()
start = text.index('extern "C" AEMU_EXPORT int rme_set_renderer_headless() {')
end = text.index('\n}', start) + 2
(out/'headless_config.inc').write_text(text[start:end])
subprocess.run([str(root/'bin/cxx'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(out),
                str(here/'headless_config_test.cpp'), '-o', str(out/'headless_test')], check=True)
subprocess.run([str(out/'headless_test')], check=True)
entries = json.loads((build/'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('/gl-host-common/opengles.cpp'))
cmd = shlex.split(entry['command'])
cmd[cmd.index('-c') + 1] = str(patched)
for option in ('-o', '-MF', '-MQ'):
    if option in cmd:
        pos = cmd.index(option)
        cmd[pos+1] = str(out/'opengles-headless.o') + ('.d' if option == '-MF' else '')
subprocess.run(cmd, cwd=entry['directory'], check=True)
print('ANDROID_HEADLESS_AARCH64_OBJECT_OK')
