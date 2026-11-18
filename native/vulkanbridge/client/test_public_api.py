#!/usr/bin/env python3
"""Exercise the public ABI generator against the exact upstream registry."""
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
UPSTREAM = Path(os.environ.get("RME_GFXSTREAM_SOURCE", "/var/tmp/rme-gfxstream-build.hTnuwsWV/src"))
BUILD = Path(os.environ.get("RME_GFXSTREAM_BUILD", "/var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest"))


class PublicApiTests(unittest.TestCase):
    def test_registry_exports_real_typed_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp) / "public.c"
            proc = subprocess.run(["python3", str(HERE / "generate_public_api.py"),
                str(UPSTREAM / "guest/mesa/src/vulkan/registry/vk.xml"),
                str(BUILD / "guest/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c"),
                str(generated)], text=True, capture_output=True, check=True)
            self.assertIn("core 1.0-1.2: 178", proc.stdout)
            code = generated.read_text()
            self.assertIn("vk_tramp_QueueSubmit(queue, submitCount, pSubmits, fence)", code)
            self.assertIn("vk_tramp_CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance)", code)
            self.assertIn("dispatch_table.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices)", code)
            self.assertIn("vk_instance_get_proc_addr(NULL, &gfxstream_vk_instance_entrypoints, pName)", code)
            self.assertIn("gfxstream_vk_GetInstanceProcAddr(instance, pName)", code)
            self.assertIn("vk_tramp_GetDeviceProcAddr(device, pName)", code)
            self.assertIn("vkCreateAndroidSurfaceKHR", code)
            self.assertIn("rme_android_surface_create_vk(pCreateInfo->window", code)
            self.assertIn("rme_android_surface_destroy_vk(surface)", code)
            self.assertIn("rme_swapchain_create_vk(pSwapchain)", code)
            self.assertIn("rme_swapchain_images_vk(swapchain, pSwapchainImageCount, pSwapchainImages", code)
            self.assertIn("rme_swapchain_acquire_vk(swapchain, timeout, semaphore, fence, pImageIndex", code)
            self.assertIn("rme_swapchain_present_vk(queue, pPresentInfo", code)
            instance_lookup = code.split("VKAPI_CALL vkGetInstanceProcAddr(", 1)[1].split("\n}\n", 1)[0]
            before_readiness = instance_lookup.split("if (rme_is_swapchain_proc(pName)", 1)[0]
            for entry in ("vkCreateSwapchainKHR", "vkDestroySwapchainKHR", "vkGetSwapchainImagesKHR",
                          "vkAcquireNextImageKHR", "vkQueuePresentKHR"):
                self.assertIn(f'return (PFN_vkVoidFunction)&{entry};', before_readiness)
            device_lookup = code.split("VKAPI_CALL vkGetDeviceProcAddr(", 1)[1].split("\n}\n", 1)[0]
            self.assertIn("if (rme_is_swapchain_proc(pName) && !rme_swapchain_ready()) return NULL;", device_lookup)
            present = code.split("VKAPI_CALL vkQueuePresentKHR(", 1)[1].split("\n}\n", 1)[0]
            self.assertIn("if (!rme_swapchain_ready()) return VK_ERROR_SURFACE_LOST_KHR;", present)

    def test_promotions_require_matching_signature(self):
        newer = Path(os.environ.get("RME_NEWER_VK_XML", "/var/tmp/rme-mesa-android-6ekppv9b/src/src/vulkan/registry/vk.xml"))
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp) / "public.c"
            args = ["python3", str(HERE / "generate_public_api.py"),
                    str(UPSTREAM / "guest/mesa/src/vulkan/registry/vk.xml"),
                    str(BUILD / "guest/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c"),
                    str(generated)]
            subprocess.run(args + [str(newer)], check=True, capture_output=True)
            code = generated.read_text()
            self.assertIn("vkMapMemory2(VkDevice", code)
            self.assertIn("vk_tramp_MapMemory2KHR(device, pMemoryMapInfo, ppData)", code)
            self.assertNotIn("vkCmdBindDescriptorSets2(", code)
            tree = ET.parse(newer)
            command = next(c for c in tree.getroot().findall("./commands/command")
                           if c.findtext("proto/name") == "vkMapMemory2")
            command.findall("param")[-1].find("type").text = "uint64_t"
            modified = Path(tmp) / "modified.xml"
            tree.write(modified)
            subprocess.run(args + [str(modified)], check=True, capture_output=True)
            self.assertNotIn("VKAPI_CALL vkMapMemory2(", generated.read_text())


if __name__ == "__main__":
    unittest.main()
