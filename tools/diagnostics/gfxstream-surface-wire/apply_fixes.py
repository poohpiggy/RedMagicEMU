#!/usr/bin/env python3
"""Fix isolated generated surface codecs without changing the wire layout."""
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: apply_fixes.py GENERATED_OUTPUT")
output = Path(sys.argv[1]).resolve(strict=True)
guest, host = output / "guest", output / "host"
# The upstream generator treats pSupported as an input while writing the
# request. Vulkan defines it as output-only, so send a neutral initial value;
# the response is still read into the caller's original pointer unchanged.
encoder = guest / "VkEncoder.cpp"
text = encoder.read_text()
start = text.index("VkResult VkEncoder::vkGetPhysicalDeviceSurfaceSupportKHR(")
end = text.index("VkResult VkEncoder::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(", start)
body = text[start:end]
old = "  memcpy(*streamPtrPtr, (VkBool32 *)pSupported, sizeof(VkBool32));"
new = "  memset(*streamPtrPtr, 0, sizeof(VkBool32));"
if body.count(old) != 1:
    raise SystemExit("unexpected generated support-query output encoding")
text = text[:start] + body.replace(old, new, 1) + text[end:]

def rewrite_request(text, name, replacements, declarations):
    start = text.index("VkEncoder::" + name + "(")
    end = text.index("\n}\n", start) + 2
    body = text[start:end]
    cut = body.index("  if (watchdog) {")
    request, response = body[:cut], body[cut:]
    marker = "  size_t count = 0;"
    if request.count(marker) != 1:
        raise SystemExit("unexpected request count: " + name)
    request = request.replace(marker, declarations + marker, 1)
    for old, new, expected in replacements:
        if request.count(old) != expected:
            raise SystemExit("unexpected request expression: " + name + " " + old)
        request = request.replace(old, new)
    return text[:start] + request + response + text[end:]

text = rewrite_request(text, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", [
    ("(VkSurfaceCapabilitiesKHR *)(pSurfaceCapabilities)", "&rmeEmptyCapabilities", 2),
], "  VkSurfaceCapabilitiesKHR rmeEmptyCapabilities{};\n")
text = rewrite_request(text, "vkGetPhysicalDeviceSurfaceFormatsKHR", [
    ("(VkSurfaceFormatKHR *)(pSurfaceFormats + i)", "&rmeEmptySurfaceFormat", 2),
    ("memcpy(*streamPtrPtr, (uint32_t *)pSurfaceFormatCount, sizeof(uint32_t));",
     "memcpy(*streamPtrPtr, (uint32_t *)&rmeFormatRequestCount, sizeof(uint32_t));", 1),
], "  VkSurfaceFormatKHR rmeEmptySurfaceFormat{};\n"
   "  uint32_t rmeFormatRequestCount = pSurfaceFormats ? *pSurfaceFormatCount : 0;\n")
text = rewrite_request(text, "vkGetPhysicalDeviceSurfacePresentModesKHR", [
    ("memcpy(*streamPtrPtr, (uint32_t *)pPresentModeCount, sizeof(uint32_t));",
     "memcpy(*streamPtrPtr, (uint32_t *)&rmeModeRequestCount, sizeof(uint32_t));", 1),
    ("memcpy(*streamPtrPtr, (VkPresentModeKHR *)pPresentModes,\n"
     "           (*(pPresentModeCount)) * sizeof(VkPresentModeKHR));",
     "memset(*streamPtrPtr, 0, (*(pPresentModeCount)) * sizeof(VkPresentModeKHR));", 1),
], "  uint32_t rmeModeRequestCount = pPresentModes ? *pPresentModeCount : 0;\n")
encoder.write_text(text)

# Surface objects in the EMU service are created with its allocator. Never use
# callbacks received from the Guest to destroy them, even for a malformed peer.
decoder = host / "VkDecoder.cpp"
text = decoder.read_text()
start = text.index("case OP_vkDestroySurfaceKHR:")
end = text.index("case OP_", start + 8)
body = text[start:end]
old = '''      if (pAllocator) {
        transform_tohost_VkAllocationCallbacks(
            m_state, (VkAllocationCallbacks *)(pAllocator));
      }
'''
if body.count(old) != 1:
    raise SystemExit("unexpected destroy allocator transform")
body = body.replace(old, "      pAllocator = nullptr;\n", 1)
old = "vk->vkDestroySurfaceKHR(unboxed_instance, surface, pAllocator);"
if body.count(old) != 1:
    raise SystemExit("unexpected destroy driver call")
body = body.replace(old, "vk->vkDestroySurfaceKHR(unboxed_instance, surface, nullptr);", 1)
decoder.write_text(text[:start] + body + text[end:])
