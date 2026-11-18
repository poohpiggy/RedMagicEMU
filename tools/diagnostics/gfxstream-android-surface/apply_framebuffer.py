#!/usr/bin/env python3
"""Patch only the fixed upstream Vulkan subwindow failure path in a copy."""
import hashlib
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
if hashlib.sha256(data).hexdigest() != 'f1e0fa1f5e5bf034f882159f7bdb14ed7c381afa623bc4c02059a3f493790fa4':
    raise SystemExit('unexpected FrameBuffer.cpp')
source = data.decode()
old = '''                if (m_emulationGl) {
                    // NOTE: This can typically happen with software-only renderers like OSMesa.
                    destroySubWindow(m_subWin);
                    m_subWin = (EGLNativeWindowType)0;
                } else {
                    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                        << "Failed to create DisplaySurface.";
                }
'''
new = '''                if (m_displayVk) {
                    ERR("Failed to create Vulkan DisplaySurface.");
                    destroySubWindow(m_subWin);
                    m_subWin = (EGLNativeWindowType)0;
                    m_nativeWindow = (FBNativeWindowType)0;
                } else if (m_emulationGl) {
                    // NOTE: This can typically happen with software-only renderers like OSMesa.
                    destroySubWindow(m_subWin);
                    m_subWin = (EGLNativeWindowType)0;
                } else {
                    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                        << "Failed to create DisplaySurface.";
                }
'''
if source.count(old) != 1:
    raise SystemExit('unexpected display-surface failure path')
Path(sys.argv[2]).write_text(source.replace(old, new, 1))
