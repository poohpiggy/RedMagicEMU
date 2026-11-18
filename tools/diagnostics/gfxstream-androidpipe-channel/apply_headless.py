#!/usr/bin/env python3
"""Add a guarded headless option to a copy of the fixed gfxstream source."""
import hashlib
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
if hashlib.sha256(data).hexdigest() != '582dafcd790cf9da31526b8da6c7a8967c9f78916eb72d86b568e923b50c4575':
    raise SystemExit('unexpected opengles.cpp')
source = data.decode()
marker = '\nint android_setOpenglesEmulation(void* renderLib, void* eglDispatch, void* glesv2Dispatch) {'
if source.count(marker) != 1:
    raise SystemExit('unexpected opengles.cpp layout')
addition = '''
// EMU may reserve its independent window for a Guest Vulkan instance.
// Called on the renderer thread, after prepare and before renderer creation.
extern "C" AEMU_EXPORT int rme_set_renderer_headless() {
    if (!sOpenglLoggerInitialized || sRenderer) return -1;
    sRendererUsesSubWindow = false;
    return 0;
}
'''
Path(sys.argv[2]).write_text(source.replace(marker, addition + marker, 1))
