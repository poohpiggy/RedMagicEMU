#!/usr/bin/env python3
"""Produce isolated copies of fixed gfxstream sources with Android Vulkan WSI."""
import hashlib
import pathlib
import sys

def read(path, digest):
    data = pathlib.Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit(f'unexpected upstream source: {path}')
    return data.decode()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('unexpected source layout')
    return text.replace(old, new, 1)

surface = read(sys.argv[1], '54fb052e79e4afcd92f97c2ecd8c6de898101ef8618d97bb5cf887e7032db1a1')
common = read(sys.argv[2], 'f9d97155698b214d81056faf51f8627ea7da993ecdfb32721e9221d4984d39da')
out = pathlib.Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
surface = replace_once(surface, '#include "vk_util.h"', '''#include "vk_util.h"
#if defined(ANDROID)
#include <vulkan/vulkan_android.h>
#endif''')
surface = replace_once(surface, '''#else
    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
        << "Unimplemented.";
#endif''', '''#elif defined(ANDROID)
    if (!window || !instance || !vk.vkGetInstanceProcAddr) return nullptr;
    auto createAndroidSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        vk.vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    if (!createAndroidSurface) return nullptr;
    const VkAndroidSurfaceCreateInfoKHR surfaceCi = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .window = window,
    };
    if (createAndroidSurface(instance, &surfaceCi, nullptr, &surface) != VK_SUCCESS ||
        surface == VK_NULL_HANDLE) return nullptr;
#else
    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
        << "Unimplemented.";
#endif''')
common = replace_once(common, '''    std::vector<const char*> surfaceInstanceExtNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };''', '''    std::vector<const char*> surfaceInstanceExtNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };
#if defined(ANDROID)
    std::vector<const char*> androidSurfaceInstanceExtNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_android_surface", // VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
#endif
''')
common = replace_once(common, '''    bool surfaceSupported =
        extensionsSupported(instanceExts, surfaceInstanceExtNames);''', '''    bool surfaceSupported =
        extensionsSupported(instanceExts, surfaceInstanceExtNames);
#if defined(ANDROID)
    bool androidSurfaceSupported =
        extensionsSupported(instanceExts, androidSurfaceInstanceExtNames);
#endif''')
common = replace_once(common, '''    if (sVkEmulation->features.VulkanNativeSwapchain.enabled) {
        for (auto extension : SwapChainStateVk::getRequiredInstanceExtensions())''', '''#if defined(ANDROID)
    if (androidSurfaceSupported) {
        for (auto extension : androidSurfaceInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }
#endif
    if (sVkEmulation->features.VulkanNativeSwapchain.enabled) {
#if defined(ANDROID)
        if (!androidSurfaceSupported) {
            VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(VK_ERROR_EXTENSION_NOT_PRESENT,
                "Android Vulkan surface extensions are unavailable");
        }
#endif
        for (auto extension : SwapChainStateVk::getRequiredInstanceExtensions())''')
(out / 'DisplaySurfaceVk.cpp').write_text(surface)
(out / 'VkCommonOperations.cpp').write_text(common)
