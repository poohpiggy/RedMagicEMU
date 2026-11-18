#!/usr/bin/env python3
"""Materialize an isolated AndroidPipe.cpp with an injectable real timer."""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
if args.source.resolve() == args.output.resolve():
    raise SystemExit('refusing to overwrite AndroidPipe.cpp')
source = args.source.read_bytes()
if hashlib.sha256(source).hexdigest() != '06a885c72a66cf5e5b6318357fe8083f7e5bb22ddeab055588443f9e89ea9a45':
    raise SystemExit('AndroidPipe.cpp hash differs from the fixed source')
text = source.decode('utf-8')
marker = 'AndroidPipe::~AndroidPipe() {'
if text.count(marker) != 1 or 'sGlobals()->pipeWaker.init(vmLock, {' not in text:
    raise SystemExit('unexpected AndroidPipe.cpp; refusing to patch')
addition = '''// RME: let the embedding device thread provide a real timer/event loop.
void rmeInitPipeWaker(VmLock* vmLock, const RmePipeTimer& timer) {
    sGlobals()->pipeWaker.init(vmLock, {
        [timer](DeviceContextRunner<PipeWakeCommand>*, std::function<void()> callback) {
            timer.install(timer.context, std::move(callback));
        },
        [timer](DeviceContextRunner<PipeWakeCommand>*) {
            timer.uninstall(timer.context);
        },
        [timer](DeviceContextRunner<PipeWakeCommand>*, uint64_t timeout) {
            timer.schedule(timer.context, timeout);
        }
    });
}

'''
args.output.write_text('#include "waker.h"\n' + text.replace(marker, addition + marker))
