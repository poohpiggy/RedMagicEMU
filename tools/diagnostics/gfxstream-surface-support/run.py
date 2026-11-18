#!/usr/bin/env python3
"""Run real Mesa surface-support body with an EMU-layout fake handle."""
import argparse
from pathlib import Path
import resource
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--cc', default='cc')
p.add_argument('--patched', action='store_true')
a = p.parse_args()
here = Path(__file__).resolve().parent
mesa = a.source / 'guest/mesa/src/vulkan/wsi/wsi_common.c'
text = mesa.read_text()
start = text.index('VKAPI_ATTR VkResult VKAPI_CALL\nwsi_GetPhysicalDeviceSurfaceSupportKHR(')
brace = text.index('{', start)
depth = 1
end = brace + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
with tempfile.TemporaryDirectory(prefix='rme-surface-support-') as tmp:
    path = Path(tmp)
    (path / 'mesa_support.inc').write_text(text[start:end] + '\n')
    command = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(here), '-I', str(path),
               '-I', str(a.source / 'guest/mesa/include'), str(here / 'test.c')]
    if a.patched:
        command.append(str(here / 'route.c'))
    command += ['-o', str(path / 'test')]
    subprocess.run(command, check=True)
    for case in ('supported', 'unsupported', 'missing', 'provider_error', 'foreign'):
        result = subprocess.run([str(path / 'test'), case], capture_output=True, text=True,
            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        if result.returncode:
            print(f'FAIL {case}: exit={result.returncode} {result.stderr.strip()}')
            raise SystemExit(1)
    print('PASS owned/foreign routing and provider results')
