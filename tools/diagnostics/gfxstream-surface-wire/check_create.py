#!/usr/bin/env python3
"""Check the isolated paired private Surface-creation message exists."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("guest", type=Path)
parser.add_argument("host", type=Path)
args = parser.parse_args()
guest, host = args.guest.read_text(), args.host.read_text()
name = "vkCreateEmuSurfaceRME"
if "VkEncoder::" + name + "(" not in guest or "case OP_" + name + ":" not in host:
    raise SystemExit("PRIVATE_SURFACE_CREATE_WIRE_MISSING")
print("PAIRED_PRIVATE_SURFACE_CREATE_GENERATED")
