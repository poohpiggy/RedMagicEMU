#!/usr/bin/env python3
"""Generate the non-HAL Vulkan ABI against the matching gfxstream/Mesa build.

The registry and generated Mesa trampolines remain external build inputs; their
original copyright and license notices are retained in those upstream files.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EXTENSIONS = (
    "VK_KHR_surface", "VK_KHR_android_surface", "VK_KHR_swapchain",
    "VK_KHR_get_physical_device_properties2", "VK_KHR_get_memory_requirements2",
    "VK_KHR_bind_memory2", "VK_ANDROID_external_memory_android_hardware_buffer",
    "VK_EXT_host_image_copy",
)
GLOBALS = {"vkCreateInstance", "vkEnumerateInstanceExtensionProperties",
           "vkEnumerateInstanceLayerProperties", "vkEnumerateInstanceVersion"}
TRACE_RESULTS = {"vkCreateDevice", "vkEnumeratePhysicalDevices", "vkEnumerateDeviceExtensionProperties",
                 "vkCreateImage", "vkCreateBuffer", "vkAllocateMemory", "vkCreateGraphicsPipelines",
                 "vkCreateFence", "vkCreateSemaphore", "vkCreatePipelineCache", "vkCreateRenderPass",
                 "vkCreateQueryPool", "vkCreateCommandPool", "vkCreateImageView"}
TRACE_LOOKUPS = TRACE_RESULTS | {"vkGetDeviceQueue", "vkGetDeviceQueue2", "vkGetPhysicalDeviceFormatProperties",
                                "vkGetPhysicalDeviceImageFormatProperties"}


def vulkan(element):
    return not element.get("api") or "vulkan" in element.get("api").split(",")


def main(registry, trampolines, output, promotions=None):
    root = ET.parse(registry).getroot()
    definitions = {}
    for command in root.findall("./commands/command"):
        if vulkan(command):
            name = command.get("name") or command.findtext("proto/name")
            definitions[name] = command

    names = {}
    for feature in root.findall("./feature"):
        if vulkan(feature) and feature.get("number") in {"1.0", "1.1", "1.2"}:
            for require in feature.findall("require"):
                if vulkan(require):
                    for command in require.findall("command"):
                        if vulkan(command):
                            names[command.get("name")] = None
    core_count = len(names)
    for feature in root.findall("./feature"):
        if vulkan(feature) and feature.get("number") == "1.3":
            for require in feature.findall("require"):
                if vulkan(require):
                    for command in require.findall("command"):
                        if vulkan(command) and command.get("name") not in names:
                            names[command.get("name")] = None
    for ext in root.findall("./extensions/extension"):
        if ext.get("name") in EXTENSIONS and "vulkan" in ext.get("supported", "").split(","):
            guard = "VK_USE_PLATFORM_ANDROID_KHR" if ext.get("platform") == "android" else None
            for require in ext.findall("require"):
                if vulkan(require):
                    for command in require.findall("command"):
                        if vulkan(command) and command.get("name") not in names:
                            names[command.get("name")] = guard
    tramp = Path(trampolines).read_text()
    available = set(re.findall(r'\bvk_tramp_(\w+)\s*\(', tramp))
    promoted = {}
    if promotions:
        newer = ET.parse(promotions).getroot()
        modern_types = {t.get("name") or t.findtext("name"): t
                        for t in newer.findall("./types/type")}
        old_types = {t.get("name") or t.findtext("name"): t
                     for t in root.findall("./types/type")}

        def canonical_type(name):
            seen = set()
            while name in modern_types and modern_types[name].get("alias"):
                if name in seen:
                    raise ValueError("type alias cycle")
                seen.add(name)
                name = modern_types[name].get("alias")
            return name

        def param_shape(p):
            decl = "".join(p.itertext())
            typ = p.findtext("type")
            if typ:
                decl = decl.replace(typ, canonical_type(typ), 1)
            return re.sub(r'\s+', '', decl)

        new_commands = {c.get("name") or c.findtext("proto/name"): c
                        for c in newer.findall("./commands/command") if vulkan(c)}
        core14 = {c.get("name") for f in newer.findall("./feature")
                  if vulkan(f) and f.get("number") == "1.4"
                  for req in f.findall("require") if vulkan(req)
                  for c in req.findall("command") if vulkan(c)}
        for alias, newer_alias in new_commands.items():
            core = newer_alias.get("alias")
            if not core or core in names or alias not in definitions or core not in new_commands:
                continue
            if core not in core14:
                continue
            old = definitions[alias]
            while old.get("alias"):
                old = definitions[old.get("alias")]
            modern = new_commands[core]
            if modern.get("alias") or old.findtext("proto/type") != modern.findtext("proto/type"):
                continue
            op = [p for p in old.findall("param") if vulkan(p)]
            np = [p for p in modern.findall("param") if vulkan(p)]
            if [param_shape(p) for p in op] != [param_shape(p) for p in np]:
                continue
            safe = True
            for p in op:
                old_name = p.findtext("type")
                new_name = canonical_type(old_name)
                ot, nt = old_types.get(old_name), modern_types.get(new_name)
                if ot is not None and nt is not None and ot.get("category") == "struct":
                    om = [param_shape(m) for m in ot.findall("member") if vulkan(m)]
                    nm = [param_shape(m) for m in nt.findall("member") if vulkan(m)]
                    if om != nm:
                        safe = False
                        break
            old_name = old.findtext("proto/name")
            if safe and old_name[2:] in available:
                names[core] = None
                promoted[core] = old_name
    out = ["/* Generated from matching upstream vk.xml and Mesa; see README.md.",
           " * Vulkan registry copyright 2015-2023 The Khronos Group Inc.",
           " * SPDX-License-Identifier: Apache-2.0 OR MIT", " */",
           '#include <string.h>',
           '#define vk_physical_device_trampolines rme_public_physical_device_trampolines',
           '#define vk_device_trampolines rme_public_device_trampolines',
           '#include "vk_dispatch_trampolines.c"',
           '#undef vk_physical_device_trampolines', '#undef vk_device_trampolines',
           '#include "vk_instance.h"', '#include "gfxstream_vk_entrypoints.h"', '#include "android_surface.h"',
           '#include "android_swapchain_object.h"',
           '#ifdef RME_TEST_ANDROID_READBACK_WSI', '#include "android_readback_wsi.h"', '#endif',
           '#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE', '#include "android_remote_swapchain.h"', '#endif',
           '#define RME_EXPORT __attribute__((visibility("default")))',
           '#ifdef RME_TRACE_VULKAN_PROCS',
           '#include <stdio.h>',
           '#include <unistd.h>',
           'static PFN_vkVoidFunction rme_trace_proc(const char *kind, const char *name, PFN_vkVoidFunction result) {',
           '    fprintf(stderr, "RME_VK_PROC %s %s %s\\n", kind, name ? name : "(null)", result ? "found" : "missing");',
           '    return result;',
           '}',
           'static VkResult rme_trace_create(const VkInstanceCreateInfo *info, VkResult result) {',
           '    const VkApplicationInfo *app = info->pApplicationInfo;',
           '    fprintf(stderr, "RME_VK_CREATE_INSTANCE result=%d pid=%d tid=%d app=%.48s\\n",',
           '        result, getpid(), gettid(), app && app->pApplicationName ? app->pApplicationName : "");',
           '    return result;',
           '}',
           'static VkResult rme_trace_result(const char *name, VkResult result) {',
           '    fprintf(stderr, "RME_VK_RESULT %s result=%d pid=%d tid=%d\\n", name, result, getpid(), gettid());',
           '    return result;',
           '}',
           'static void rme_trace_queue(const char *name, VkQueue queue) {',
           '    fprintf(stderr, "RME_VK_QUEUE %s %s pid=%d tid=%d\\n", name, queue ? "found" : "null", getpid(), gettid());',
           '}',
           'static void rme_trace_format(VkFormat format, const VkFormatProperties *props) {',
           '    fprintf(stderr, "RME_VK_FORMAT %d linear=%x optimal=%x buffer=%x pid=%d tid=%d\\n",',
           '        format, props->linearTilingFeatures, props->optimalTilingFeatures, props->bufferFeatures, getpid(), gettid());',
           '}',
           'static VkResult rme_trace_image_format(VkFormat format, VkImageType type, VkImageTiling tiling,',
           '    VkImageUsageFlags usage, VkImageCreateFlags flags, const VkImageFormatProperties *props, VkResult result) {',
           '    fprintf(stderr, "RME_VK_IMAGE_FORMAT %d type=%d tiling=%d usage=%x flags=%x result=%d samples=%x pid=%d tid=%d\\n",',
           '        format, type, tiling, usage, flags, result, result == VK_SUCCESS ? props->sampleCounts : 0, getpid(), gettid());',
           '    return result;',
           '}',
           '#else',
           '#define rme_trace_proc(kind, name, result) (result)',
           '#define rme_trace_create(info, result) (result)',
           '#define rme_trace_result(name, result) (result)',
           '#define rme_trace_queue(name, queue) ((void)0)',
           '#define rme_trace_format(format, props) ((void)0)',
           '#define rme_trace_image_format(format, type, tiling, usage, flags, props, result) (result)',
           '#endif',
           'static int rme_is_swapchain_proc(const char *name) {',
           '    if (!name) return 0;',
           '    static const char *const names[] = {"vkCreateSwapchainKHR", "vkDestroySwapchainKHR",',
           '        "vkGetSwapchainImagesKHR", "vkAcquireNextImageKHR", "vkAcquireNextImage2KHR",',
           '        "vkQueuePresentKHR", "vkCreateSharedSwapchainsKHR", "vkGetDeviceGroupPresentCapabilitiesKHR",',
           '        "vkGetDeviceGroupSurfacePresentModesKHR", "vkGetPhysicalDevicePresentRectanglesKHR"};',
           '    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)',
           '        if (!strcmp(name, names[i])) return 1;',
           '    return 0;',
           '}', '']
    trace_lookup = ['#ifdef RME_TRACE_VULKAN_PROCS'] + [
        f'    if (!strcmp(pName, "{name}")) return (PFN_vkVoidFunction)&{name};'
        for name in sorted(TRACE_LOOKUPS & names.keys())
    ] + ['#endif']
    readback_calls = {
        "vkGetPhysicalDeviceSurfaceSupportKHR": ("rme_android_surface_owned_vk(surface)", "rme_readback_support(physicalDevice, queueFamilyIndex, surface, pSupported)"),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR": ("rme_android_surface_owned_vk(surface)", "rme_readback_capabilities(physicalDevice, surface, pSurfaceCapabilities)"),
        "vkGetPhysicalDeviceSurfaceFormatsKHR": ("rme_android_surface_owned_vk(surface)", "rme_readback_formats(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats)"),
        "vkGetPhysicalDeviceSurfacePresentModesKHR": ("rme_android_surface_owned_vk(surface)", "rme_readback_modes(physicalDevice, surface, pPresentModeCount, pPresentModes)"),
        "vkCreateSwapchainKHR": ("rme_android_surface_owned_vk(pCreateInfo->surface)", "rme_readback_create(device, pCreateInfo, pAllocator, pSwapchain)"),
    }
    readback_dispatch = {
        "vkGetSwapchainImagesKHR": "rme_readback_images(swapchain, pSwapchainImageCount, pSwapchainImages, &readback)",
        "vkAcquireNextImageKHR": "rme_readback_acquire(swapchain, timeout, semaphore, fence, pImageIndex, &readback)",
        "vkAcquireNextImage2KHR": "pAcquireInfo->deviceMask == 1 && rme_readback_acquire(pAcquireInfo->swapchain, pAcquireInfo->timeout, pAcquireInfo->semaphore, pAcquireInfo->fence, pImageIndex, &readback)",
        "vkQueuePresentKHR": "rme_readback_present(queue, pPresentInfo, &readback)",
    }
    for name, guard in names.items():
        node = definitions[promoted.get(name, name)]
        canonical = promoted.get(name, name)
        while node.get("alias"):
            canonical = node.get("alias")
            node = definitions[canonical]
        proto = node.find("proto")
        ret = proto.findtext("type")
        params = [p for p in node.findall("param") if vulkan(p)]
        declaration = ", ".join("".join(p.itertext()) for p in params) or "void"
        args = ", ".join(p.findtext("name") for p in params)
        first = params[0].findtext("type") if params else None
        if guard:
            out.append("#ifdef " + guard)
        out.append(f"RME_EXPORT VKAPI_ATTR {ret} VKAPI_CALL {name}({declaration})")
        out.append("{")
        if name in readback_calls:
            condition, call = readback_calls[name]
            out.extend(("#ifdef RME_TEST_ANDROID_READBACK_WSI", f'    if ({condition}) return rme_trace_result("{name}", {call});', "#endif"))
        if name in readback_dispatch:
            out.extend(("#ifdef RME_TEST_ANDROID_READBACK_WSI", "    VkResult readback;",
                        f"    if ({readback_dispatch[name]}) return readback;", "#endif"))
        if name == "vkDestroyInstance":
            out.append("    if (instance == VK_NULL_HANDLE) return;")
        elif name == "vkDestroyDevice":
            out.append("    if (device == VK_NULL_HANDLE) return;")
        elif name == "vkDestroySurfaceKHR":
            out.append("    if (surface == VK_NULL_HANDLE) return;")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner; VkSurfaceKHR rme_remote;")
            out.append("    if (rme_android_surface_remote_info_vk(surface, &rme_owner, &rme_remote) &&")
            out.append("        !rme_android_surface_wire_ready(rme_owner)) return;")
            out.append("#endif")
            out.append("    if (rme_android_surface_destroy_vk(surface)) return;")
        elif name == "vkDestroySwapchainKHR":
            out.append("    if (swapchain == VK_NULL_HANDLE) return;")
            out.extend(("#ifdef RME_TEST_ANDROID_READBACK_WSI", "    if (rme_readback_destroy(swapchain)) return;", "#endif"))
            out.append("    if (rme_swapchain_destroy_vk(swapchain)) return;")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner;")
            out.append("    if (rme_remote_swapchain_owner(swapchain, &rme_owner)) {")
            out.append("        if (rme_android_surface_wire_ready(rme_owner) && rme_remote_swapchain_forget(swapchain))")
            out.append("            rme_android_surface_encoder_destroy_swapchain(device, swapchain, pAllocator);")
            out.append("        return;")
            out.append("    }")
            out.append("#endif")
        if name == "vkGetInstanceProcAddr":
            out += [
                '    if (pName && strcmp(pName, "vkGetInstanceProcAddr") == 0)',
                '        return (PFN_vkVoidFunction)&vkGetInstanceProcAddr;',
                '#ifdef RME_TRACE_VULKAN_PROCS',
                '    if (pName && strcmp(pName, "vkCreateInstance") == 0)',
                '        return (PFN_vkVoidFunction)&vkCreateInstance;',
                '#endif',
                '    if (!instance)',
                '        return rme_trace_proc("global", pName, vk_instance_get_proc_addr(NULL, &gfxstream_vk_instance_entrypoints, pName));',
                '#ifdef VK_USE_PLATFORM_ANDROID_KHR',
                '    /* Android clients resolve these loader entry points before creating a device. */',
                '    if (pName) {',
                '        if (!strcmp(pName, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)&vkCreateSwapchainKHR;',
                '        if (!strcmp(pName, "vkDestroySwapchainKHR")) return (PFN_vkVoidFunction)&vkDestroySwapchainKHR;',
                '        if (!strcmp(pName, "vkGetSwapchainImagesKHR")) return (PFN_vkVoidFunction)&vkGetSwapchainImagesKHR;',
                '        if (!strcmp(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)&vkAcquireNextImageKHR;',
                '        if (!strcmp(pName, "vkQueuePresentKHR")) return (PFN_vkVoidFunction)&vkQueuePresentKHR;',
                '    }',
                '#endif',
                '    if (rme_is_swapchain_proc(pName) && !rme_swapchain_ready()) return NULL;',
                '#ifdef VK_USE_PLATFORM_ANDROID_KHR',
                '    if (pName && !strcmp(pName, "vkCreateAndroidSurfaceKHR")) {',
                '        VK_FROM_HANDLE(vk_instance, rme_instance, instance);',
                '        return rme_instance->enabled_extensions.KHR_android_surface',
                '            ? (PFN_vkVoidFunction)&vkCreateAndroidSurfaceKHR : NULL;',
                '    }',
                '#endif',
                '    PFN_vkVoidFunction result = rme_trace_proc("instance", pName, gfxstream_vk_GetInstanceProcAddr(instance, pName));',
                '    if (!result || !pName) return result;',
                *trace_lookup,
                '    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)&vkGetDeviceProcAddr;',
                '    if (!strcmp(pName, "vkDestroySurfaceKHR")) return (PFN_vkVoidFunction)&vkDestroySurfaceKHR;',
                '    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceSupportKHR")) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceSurfaceSupportKHR;',
                '    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceSurfaceCapabilitiesKHR;',
                '    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR")) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceSurfaceFormatsKHR;',
                '    if (!strcmp(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR")) return (PFN_vkVoidFunction)&vkGetPhysicalDeviceSurfacePresentModesKHR;',
                '    if (!strcmp(pName, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)&vkCreateSwapchainKHR;',
                '    if (!strcmp(pName, "vkDestroySwapchainKHR")) return (PFN_vkVoidFunction)&vkDestroySwapchainKHR;',
                '    if (!strcmp(pName, "vkGetSwapchainImagesKHR")) return (PFN_vkVoidFunction)&vkGetSwapchainImagesKHR;',
                '    if (!strcmp(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)&vkAcquireNextImageKHR;',
                '    if (!strcmp(pName, "vkAcquireNextImage2KHR")) return (PFN_vkVoidFunction)&vkAcquireNextImage2KHR;',
                '    if (!strcmp(pName, "vkQueuePresentKHR")) return (PFN_vkVoidFunction)&vkQueuePresentKHR;',
                '    return result;',
            ]
        elif name == "vkGetDeviceProcAddr":
            out += [
                '#ifndef RME_TEST_ANDROID_READBACK_WSI',
                '    if (rme_is_swapchain_proc(pName) && !rme_swapchain_ready()) return NULL;',
                '#endif',
                '    PFN_vkVoidFunction result = rme_trace_proc("device", pName, vk_tramp_GetDeviceProcAddr(device, pName));',
                '    if (!result || !pName) return result;',
                *trace_lookup,
                '    if (!strcmp(pName, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)&vkCreateSwapchainKHR;',
                '    if (!strcmp(pName, "vkDestroySwapchainKHR")) return (PFN_vkVoidFunction)&vkDestroySwapchainKHR;',
                '    if (!strcmp(pName, "vkGetSwapchainImagesKHR")) return (PFN_vkVoidFunction)&vkGetSwapchainImagesKHR;',
                '    if (!strcmp(pName, "vkAcquireNextImageKHR")) return (PFN_vkVoidFunction)&vkAcquireNextImageKHR;',
                '    if (!strcmp(pName, "vkAcquireNextImage2KHR")) return (PFN_vkVoidFunction)&vkAcquireNextImage2KHR;',
                '    if (!strcmp(pName, "vkQueuePresentKHR")) return (PFN_vkVoidFunction)&vkQueuePresentKHR;',
                '    return result;',
            ]
        elif name == "vkCreateAndroidSurfaceKHR":
            out.append("    VK_FROM_HANDLE(vk_instance, rme_instance, instance);")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    if (rme_android_surface_wire_ready(instance))")
            out.append("        return rme_android_surface_create_encoded_vk(instance, pCreateInfo->window, pAllocator ? pAllocator : &rme_instance->alloc, pSurface);")
            out.append("#endif")
            out.append('    return rme_trace_result("vkCreateAndroidSurfaceKHR", rme_android_surface_create_vk(pCreateInfo->window, pAllocator ? pAllocator : &rme_instance->alloc, pSurface));')
        elif name == "vkGetPhysicalDeviceSurfaceSupportKHR":
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner; VkSurfaceKHR rme_remote;")
            out.append("    if (rme_android_surface_remote_info_vk(surface, &rme_owner, &rme_remote)) {")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) { *pSupported = VK_FALSE; return VK_ERROR_SURFACE_LOST_KHR; }")
            out.append("        return rme_android_surface_encoder_support(physicalDevice, queueFamilyIndex, rme_remote, pSupported);")
            out.append("    }")
            out.append("#endif")
            out.append("    if (rme_android_surface_owned_vk(surface)) { *pSupported = VK_FALSE; return VK_ERROR_SURFACE_LOST_KHR; }")
            out.append("    return vk_tramp_GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);")
        elif name in {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", "vkGetPhysicalDeviceSurfaceFormatsKHR", "vkGetPhysicalDeviceSurfacePresentModesKHR"}:
            remote_call = {
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR": "rme_android_surface_encoder_capabilities(physicalDevice, rme_remote, pSurfaceCapabilities)",
                "vkGetPhysicalDeviceSurfaceFormatsKHR": "rme_android_surface_encoder_formats(physicalDevice, rme_remote, pSurfaceFormatCount, pSurfaceFormats)",
                "vkGetPhysicalDeviceSurfacePresentModesKHR": "rme_android_surface_encoder_modes(physicalDevice, rme_remote, pPresentModeCount, pPresentModes)",
            }[name]
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner; VkSurfaceKHR rme_remote;")
            out.append("    if (rme_android_surface_remote_info_vk(surface, &rme_owner, &rme_remote)) {")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append(f"        return {remote_call};")
            out.append("    }")
            out.append("#endif")
            out.append("    if (rme_android_surface_owned_vk(surface)) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append(f"    return vk_tramp_{name[2:]}({args});")
        elif name == "vkCreateSwapchainKHR":
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner; VkSurfaceKHR rme_remote;")
            out.append("    if (rme_android_surface_remote_info_vk(pCreateInfo->surface, &rme_owner, &rme_remote)) {")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("        VkSwapchainCreateInfoKHR rme_info = *pCreateInfo; rme_info.surface = rme_remote;")
            out.append("        return rme_remote_swapchain_create(rme_owner, device, &rme_info, pAllocator, pSwapchain);")
            out.append("    }")
            out.append("#endif")
            out.append("    (void)device; (void)pCreateInfo; (void)pAllocator;")
            out.append("    return rme_swapchain_create_vk(pSwapchain);")
        elif name == "vkGetSwapchainImagesKHR":
            out.append("    if (!rme_swapchain_ready()) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner;")
            out.append("    if (rme_remote_swapchain_owner(swapchain, &rme_owner)) {")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("        return rme_android_surface_encoder_swapchain_images(device, swapchain, pSwapchainImageCount, pSwapchainImages);")
            out.append("    }")
            out.append("#endif")
            out.append("    VkResult result;")
            out.append("    if (rme_swapchain_images_vk(swapchain, pSwapchainImageCount, pSwapchainImages, &result)) return result;")
            out.append("    return vk_tramp_GetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);")
        elif name == "vkAcquireNextImageKHR":
            out.append("    if (!rme_swapchain_ready()) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    VkInstance rme_owner;")
            out.append("    if (rme_remote_swapchain_owner(swapchain, &rme_owner)) {")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("        uint32_t rme_index = 0;")
            out.append("        VkResult rme_result = rme_android_surface_encoder_acquire(device, swapchain, timeout, semaphore, fence, &rme_index);")
            out.append("        if (rme_result == VK_SUCCESS || rme_result == VK_SUBOPTIMAL_KHR) *pImageIndex = rme_index;")
            out.append("        return rme_result;")
            out.append("    }")
            out.append("#endif")
            out.append("    VkResult result;")
            out.append("    if (rme_swapchain_acquire_vk(swapchain, timeout, semaphore, fence, pImageIndex, &result)) return result;")
            out.append("    return vk_tramp_AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);")
        elif name == "vkAcquireNextImage2KHR":
            out.append("    VkResult result;")
            out.append("    if (rme_swapchain_owns(pAcquireInfo->swapchain)) {")
            out.append("        if (pAcquireInfo->deviceMask != 1) return VK_ERROR_FEATURE_NOT_PRESENT;")
            out.append("        if (rme_swapchain_acquire_vk(pAcquireInfo->swapchain, pAcquireInfo->timeout, pAcquireInfo->semaphore, pAcquireInfo->fence, pImageIndex, &result)) return result;")
            out.append("    }")
            out.append("    return vk_tramp_AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);")
        elif name == "vkQueuePresentKHR":
            out.append("    if (!rme_swapchain_ready()) return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("#ifdef RME_EXPERIMENTAL_REMOTE_SURFACE")
            out.append("    for (uint32_t rme_i = 0; rme_i < pPresentInfo->swapchainCount; ++rme_i) {")
            out.append("        VkInstance rme_owner;")
            out.append("        if (!rme_remote_swapchain_owner(pPresentInfo->pSwapchains[rme_i], &rme_owner)) continue;")
            out.append("        if (pPresentInfo->swapchainCount != 1 || pPresentInfo->pNext) {")
            out.append("            if (pPresentInfo->pResults) for (uint32_t rme_j = 0; rme_j < pPresentInfo->swapchainCount; ++rme_j) pPresentInfo->pResults[rme_j] = VK_ERROR_OUT_OF_DATE_KHR;")
            out.append("            return VK_ERROR_OUT_OF_DATE_KHR;")
            out.append("        }")
            out.append("        if (!rme_android_surface_wire_ready(rme_owner)) {")
            out.append("            if (pPresentInfo->pResults) pPresentInfo->pResults[0] = VK_ERROR_SURFACE_LOST_KHR;")
            out.append("            return VK_ERROR_SURFACE_LOST_KHR;")
            out.append("        }")
            out.append("        return rme_android_surface_encoder_present(queue, pPresentInfo);")
            out.append("    }")
            out.append("#endif")
            out.append("    VkResult result;")
            out.append("    if (rme_swapchain_present_vk(queue, pPresentInfo, &result)) return result;")
            out.append("    return vk_tramp_QueuePresentKHR(queue, pPresentInfo);")
        else:
            if name == "vkCreateInstance":
                call = f"rme_trace_create(pCreateInfo, gfxstream_vk_CreateInstance({args}))"
            elif name in GLOBALS:
                call = f"gfxstream_vk_{name[2:]}({args})"
            elif first == "VkInstance":
                out.append('    VK_FROM_HANDLE(vk_instance, rme_instance, instance);')
                call = f"rme_instance->dispatch_table.{canonical[2:]}({args})"
            else:
                suffix = canonical[2:]
                if suffix not in available:
                    raise ValueError(f"missing Mesa trampoline: {name} ({canonical})")
                call = f"vk_tramp_{suffix}({args})"
            if name in TRACE_RESULTS:
                call = f'rme_trace_result("{name}", {call})'
            elif name in {"vkGetDeviceQueue", "vkGetDeviceQueue2"}:
                out.append(f"    {call};")
                call = f'rme_trace_queue("{name}", *pQueue)'
            elif name == "vkGetPhysicalDeviceFormatProperties":
                out.append(f"    {call};")
                call = "rme_trace_format(format, pFormatProperties)"
            elif name == "vkGetPhysicalDeviceImageFormatProperties":
                call = f"rme_trace_image_format(format, type, tiling, usage, flags, pImageFormatProperties, {call})"
            out.append(f"    {'return ' if ret != 'void' else ''}{call};")
        out.append("}")
        if guard:
            out.append("#endif")
        out.append("")
    Path(output).write_text("\n".join(out))
    print(f"core 1.0-1.2: {core_count}; promoted aliases: {len(promoted)}; total exports: {len(names)}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit("usage: generate_public_api.py VK_XML MESA_TRAMPOLINES OUT_C [NEWER_VK_XML]")
    main(*sys.argv[1:])
