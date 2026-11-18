#!/usr/bin/env python3
"""Test the d7a swapchain metadata candidate without a renderer or a GPU."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('--patch', type=Path)
args = parser.parse_args()
paths = [Path('host/vulkan') / name for name in ('VkDecoderGlobalState.cpp', 'VkDecoderGlobalState.h', 'VkDecoder.cpp')]
with tempfile.TemporaryDirectory(prefix='rme-swapchain-meta-') as tmp:
    root = Path(tmp)
    for path in paths:
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((args.source / path).read_bytes())
    if args.patch:
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(args.patch.resolve())], cwd=root, check=True)
    state = (root / paths[0]).read_text()
    decoder = (root / paths[2]).read_text()
    create = decoder.split('case OP_vkCreateSwapchainKHR:', 1)[1].split('case OP_vkDestroySwapchainKHR:', 1)[0]
    destroy = decoder.split('case OP_vkDestroySwapchainKHR:', 1)[1].split('case OP_vkGetSwapchainImagesKHR:', 1)[0]
    if 'on_recordSwapchain' not in create or 'on_forgetSwapchain' not in destroy:
        raise SystemExit('FAIL: decoder does not track swapchain creation and destruction')
    assert create.index('mapHandles_VkSwapchainKHR') < create.index('on_recordSwapchain') < create.index('vkStream->write((VkSwapchainKHR*)pSwapchain')
    assert 'if (vkCreateSwapchainKHR_VkResult_return == VK_SUCCESS)\n                    m_state->on_recordSwapchain' in create
    assert destroy.index('on_forgetSwapchain') < destroy.index('vk->vkDestroySwapchainKHR')
    assert 'forgetDeviceSwapchainsLocked(device)' in state
    assert 'mSwapchainImageMetadata.clear();' in state
    start = state.index('    // RME swapchain metadata begin')
    end = state.index('    // RME swapchain metadata end', start)
    methods = state[start:end]
    harness = Path(__file__).with_name('harness.cpp').read_text().replace('/* EXTRACTED_METHODS */', methods)
    cpp = root / 'test.cpp'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(root / 'test'),
                    *shlex.split(os.environ.get('LDFLAGS', ''))], check=True)
    subprocess.run([str(root / 'test')], check=True)
