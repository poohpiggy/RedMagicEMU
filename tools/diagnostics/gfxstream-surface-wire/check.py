#!/usr/bin/env python3
"""Check the generated paired surface-query and destruction command paths."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("guest", type=Path)
parser.add_argument("host", type=Path)
args = parser.parse_args()
guest, host = args.guest.read_text(), args.host.read_text()
commands = (
    "vkDestroySurfaceKHR",
    "vkGetPhysicalDeviceSurfaceSupportKHR",
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
    "vkGetPhysicalDeviceSurfaceFormatsKHR",
    "vkGetPhysicalDeviceSurfacePresentModesKHR",
)
missing = []
for command in commands:
    if "VkEncoder::" + command + "(" not in guest:
        missing.append("guest " + command)
    if "case OP_" + command + ":" not in host:
        missing.append("host " + command)
if missing:
    raise SystemExit("MISSING_SURFACE_WIRE: " + ", ".join(missing))
print("PAIRED_SURFACE_QUERY_AND_DESTROY_GENERATED")
