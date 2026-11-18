#!/usr/bin/env python3
"""Verify the fixed sources include Android WSI creation and capability selection."""
import pathlib
import sys

surface = pathlib.Path(sys.argv[1]).read_text()
common = pathlib.Path(sys.argv[2]).read_text()
assert '"vkCreateAndroidSurfaceKHR"' in surface, 'Android Vulkan surface creation absent'
assert 'VK_KHR_ANDROID_SURFACE_EXTENSION_NAME' in common, 'Android surface extension not selected'
print('ANDROID_SURFACE_SOURCE_REQUIREMENTS_OK')
