#!/usr/bin/env python3
"""Wire the isolated private create opcode to the EMU-owned host Surface."""
import re
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: apply_create.py GENERATED_OUTPUT")
root = Path(sys.argv[1]).resolve(strict=True)
guest, host = root / "guest/VkEncoder.cpp", root / "host/VkDecoder.cpp"

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("unexpected generated create statement: " + old[:80])
    return text.replace(old, new, 1)

# This is an EMU protocol command, never a real Vulkan driver entry point.
# Keep the existing VulkanDispatch layout and do not ask the driver for it.
dispatch_h = root / "host/cereal/common/goldfish_vk_dispatch.h"
data = dispatch_h.read_text()
data = replace_once(data, "  PFN_vkCreateEmuSurfaceRME vkCreateEmuSurfaceRME;\n", "")
dispatch_h.write_text(data)
dispatch_cpp = root / "host/cereal/common/goldfish_vk_dispatch.cpp"
data = dispatch_cpp.read_text()
data, removed = re.subn(r"  out->vkCreateEmuSurfaceRME\s*=\s*\(PFN_vkCreateEmuSurfaceRME\)[^;]+;\n", "", data)
if removed != 3:
    raise SystemExit("unexpected private driver dispatch count")
dispatch_cpp.write_text(data)

text = guest.read_text()
start = text.index("VkEncoder::vkCreateEmuSurfaceRME(")
end = text.index("\n}\n", start) + 2
body = text[start:end]
body = replace_once(body, "*&cgen_var_1 = (uint64_t)((*pSurface));", "cgen_var_1 = 0;")
old = '''  uint64_t cgen_var_2;
  stream->read((uint64_t *)&cgen_var_2, 8);
  stream->handleMapping()->mapHandles_u64_VkSurfaceKHR(
      &cgen_var_2, (VkSurfaceKHR *)pSurface, 1);
  VkResult vkCreateEmuSurfaceRME_VkResult_return = (VkResult)0;
  stream->read(&vkCreateEmuSurfaceRME_VkResult_return, sizeof(VkResult));
'''
new = '''  uint64_t cgen_var_2;
  stream->read((uint64_t *)&cgen_var_2, 8);
  VkResult vkCreateEmuSurfaceRME_VkResult_return = (VkResult)0;
  stream->read(&vkCreateEmuSurfaceRME_VkResult_return, sizeof(VkResult));
  if (vkCreateEmuSurfaceRME_VkResult_return == VK_SUCCESS && cgen_var_2) {
    stream->setHandleMapping(sResourceTracker->createMapping());
    stream->handleMapping()->mapHandles_u64_VkSurfaceKHR(
        &cgen_var_2, (VkSurfaceKHR *)pSurface, 1);
    stream->unsetHandleMapping();
  } else {
    *pSurface = VK_NULL_HANDLE;
    if (vkCreateEmuSurfaceRME_VkResult_return == VK_SUCCESS)
      vkCreateEmuSurfaceRME_VkResult_return = VK_ERROR_INITIALIZATION_FAILED;
  }
'''
body = replace_once(body, old, new)
guest.write_text(text[:start] + body + text[end:])

text = host.read_text()
text = replace_once(text, '#include "VkDecoder.h"',
                    '#include "VkDecoder.h"\n#include "host_guest_surface.h"')
start = text.index("case OP_vkCreateEmuSurfaceRME:")
end = text.index("case OP_", start + 8)
body = text[start:end]
body = replace_once(body, "vk->vkCreateEmuSurfaceRME(unboxed_instance, pSurface)",
                    "rme_vk_guest_surface_create(unboxed_instance, vk->vkGetInstanceProcAddr, pSurface)")
old = '''      vkStream->unsetHandleMapping();
      uint64_t cgen_var_2;
      vkStream->handleMapping()->mapHandles_VkSurfaceKHR_u64(pSurface,
                                                             &cgen_var_2, 1);
      vkStream->write((uint64_t *)&cgen_var_2, 8);
'''
new = '''      vkStream->unsetHandleMapping();
      if (vkCreateEmuSurfaceRME_VkResult_return == VK_SUCCESS)
        vkStream->setHandleMapping(&m_boxedHandleCreateMapping);
      else
        *pSurface = VK_NULL_HANDLE;
      uint64_t cgen_var_2;
      vkStream->handleMapping()->mapHandles_VkSurfaceKHR_u64(pSurface,
                                                             &cgen_var_2, 1);
      vkStream->write((uint64_t *)&cgen_var_2, 8);
      vkStream->setHandleMapping(&m_boxedHandleUnwrapMapping);
'''
body = replace_once(body, old, new)
text = text[:start] + body + text[end:]
start = text.index("case OP_vkDestroySurfaceKHR:")
end = text.index("case OP_", start + 8)
body = text[start:end]
body = replace_once(body, "vk->vkDestroySurfaceKHR(unboxed_instance, surface, nullptr);",
                    "const bool rmeDestroyed = rme_vk_guest_surface_destroy(unboxed_instance, surface);\n"
                    "      if (!rmeDestroyed) fprintf(stderr, \"EMU surface owner mismatch\\n\");")
body = replace_once(body, "delete_VkSurfaceKHR(boxed_surface_preserve);",
                    "if (rmeDestroyed) delete_VkSurfaceKHR(boxed_surface_preserve);")
host.write_text(text[:start] + body + text[end:])
