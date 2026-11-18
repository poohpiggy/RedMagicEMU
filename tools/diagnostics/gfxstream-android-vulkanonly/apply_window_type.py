#!/usr/bin/env python3
"""Patch Android's non-GLES window type in a separate fixed-source copy."""
import hashlib
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_bytes()
if hashlib.sha256(source).hexdigest() != '6b2fb114ffa0f3cf14dc00b87b4db49fe8425ce2582611c6844a5b9069ca3dab':
    raise SystemExit('unexpected GlesCompat.h')
text = source.decode()
old = 'typedef unsigned int EGLNativeWindowType;'
new = '''#if defined(ANDROID)
struct ANativeWindow;
typedef ANativeWindow* EGLNativeWindowType;
#else
typedef unsigned int EGLNativeWindowType;
#endif'''
if text.count(old) != 1:
    raise SystemExit('unexpected native window type declaration')
Path(sys.argv[2]).write_text(text.replace(old, new, 1))
