#!/usr/bin/env python3
"""Offline exercise of extracted d7a swapchain-image lifecycle methods."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('--patch', type=Path)
parser.add_argument('--decoder', type=Path, help='use a regenerated VkDecoder.cpp for the decoder checks')
args = parser.parse_args()
here = Path(__file__).resolve().parent
metadata = here.parent / 'gfxstream-swapchain-metadata' / 'fix.patch'
names = ['VkDecoderGlobalState.cpp', 'VkDecoderGlobalState.h', 'VkDecoder.cpp']
with tempfile.TemporaryDirectory(prefix='rme-swapchain-images-') as tmp:
    root = Path(tmp)
    relative = Path('host/vulkan')
    for name in names:
        target = root / relative / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((args.source / relative / name).read_bytes())
    for patch in [metadata, *([args.patch] if args.patch else [])]:
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch.resolve())], cwd=root, check=True)
    state = (root / relative / names[0]).read_text()
    decoder = args.decoder.read_text() if args.decoder else (root / relative / names[2]).read_text()
    create = decoder.split('case OP_vkCreateSwapchainKHR:', 1)[1].split('case OP_vkDestroySwapchainKHR:', 1)[0]
    destroy = decoder.split('case OP_vkDestroySwapchainKHR:', 1)[1].split('case OP_vkGetSwapchainImagesKHR:', 1)[0]
    get = decoder.split('case OP_vkGetSwapchainImagesKHR:', 1)[1].split('case OP_vkAcquireNextImageKHR:', 1)[0]
    if 'on_registerSwapchainImages' not in get:
        raise SystemExit('FAIL: swapchain enumeration returns unregistered driver images')
    assert create.index('mapHandles_VkSwapchainKHR') < create.index('on_recordSwapchain') < create.index('commitWrite()')
    assert re.search(r'if\s*\(vkCreateSwapchainKHR_VkResult_return\s*==\s*VK_SUCCESS\)\s*(?:\{\s*)?m_state->on_recordSwapchain', create)
    assert destroy.index('on_forgetSwapchain') < destroy.index('vk->vkDestroySwapchainKHR')
    assert 'VK_INCOMPLETE' in get and 'on_validateSwapchain' in get
    assert get.index('on_validateSwapchain') < get.index('vk->vkGetSwapchainImagesKHR') < get.index('on_registerSwapchainImages')
    assert '*pSwapchainImageCount = 0' in get
    start = get.index('VkResult vkGetSwapchainImagesKHR_VkResult_return =')
    end = get.index('if ((vkGetSwapchainImagesKHR_VkResult_return) == VK_ERROR_DEVICE_LOST)', start)
    dispatch = get[start:end]
    start = state.index('    // RME swapchain metadata begin')
    end = state.index('    // RME swapchain metadata end', start)
    methods = state[start:end]
    start = state.index('    void destroyImageLocked(')
    end = state.index('    void on_vkDestroyImage(', start)
    destroy = state[start:end]
    harness = (here / 'harness.cpp').read_text().replace('/* EXTRACTED_METHODS */', methods).replace('/* EXTRACTED_DESTROY */', destroy).replace('/* EXTRACTED_DISPATCH */', dispatch)
    cpp = root / 'test.cpp'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(root / 'test'),
                    *shlex.split(os.environ.get('LDFLAGS', ''))], check=True)
    subprocess.run([str(root / 'test')], check=True)
