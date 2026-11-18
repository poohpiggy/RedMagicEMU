#!/usr/bin/env python3
"""Create a fixed-source candidate; never modify the supplied gfxstream tree."""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
if args.source.resolve() == args.output.resolve():
    raise SystemExit('refusing to overwrite source')
source = args.source.read_bytes()
if hashlib.sha256(source).hexdigest() != 'f9d97155698b214d81056faf51f8627ea7da993ecdfb32721e9221d4984d39da':
    raise SystemExit('expected merged d7a physical-device-selection source')
text = source.decode('utf-8')

def insert_once(old, new):
    global text
    if text.count(old) != 1:
        raise SystemExit('unexpected source structure: ' + old[:80])
    text = text.replace(old, new, 1)

insert_once('#include "VkCommonOperations.h"', '''#include "VkCommonOperations.h"
#if defined(ANDROID)
#include "approved_gpu_identity.h"
#endif''')
marker = '    std::vector<uint32_t> deviceScores(physdevCount, 0);'
identity_gate = '''#if defined(ANDROID)
    // The provider is responsible for existing GPU policy checks. Selection is
    // forbidden unless this instance finds exactly the approved device/driver.
    ApprovedVulkanIdentity approvedIdentity{};
    static_assert(sizeof(approvedIdentity.deviceUuid) == VK_UUID_SIZE, "UUID size");
    static_assert(sizeof(approvedIdentity.driverUuid) == VK_UUID_SIZE, "UUID size");
    bool hasDeviceUuid = false, hasDriverUuid = false;
    if (!getApprovedVulkanIdentity(&approvedIdentity)) {
        VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(ABORT_REASON_OTHER, "Approved GPU identity unavailable.");
    }
    for (size_t j = 0; j < VK_UUID_SIZE; ++j) {
        hasDeviceUuid |= approvedIdentity.deviceUuid[j] != 0;
        hasDriverUuid |= approvedIdentity.driverUuid[j] != 0;
    }
    if (!hasDeviceUuid || !hasDriverUuid) {
        VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(ABORT_REASON_OTHER, "Approved GPU identity is empty.");
    }
    uint32_t rmeApprovedIndex = physdevCount;
    for (uint32_t i = 0; i < physdevCount; ++i) {
        const auto& candidate = deviceInfos[i];
        if (!candidate.supportsIdProperties ||
            memcmp(candidate.idProps.deviceUUID, approvedIdentity.deviceUuid, VK_UUID_SIZE) ||
            memcmp(candidate.idProps.driverUUID, approvedIdentity.driverUuid, VK_UUID_SIZE)) continue;
        if (rmeApprovedIndex != physdevCount) {
            VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(ABORT_REASON_OTHER, "Approved GPU identity is ambiguous.");
        }
        rmeApprovedIndex = i;
    }
    if (rmeApprovedIndex == physdevCount) {
        VK_EMU_INIT_RETURN_OR_ABORT_ON_ERROR(ABORT_REASON_OTHER, "Approved GPU identity not found.");
    }
#endif

'''
insert_once(marker, identity_gate + marker)
marker = '''    for (uint32_t i = 0; i < physdevCount; ++i) {
        const auto& info = deviceInfos[i];'''
insert_once(marker, '''    for (uint32_t i = 0; i < physdevCount; ++i) {
#if defined(ANDROID)
        if (i != rmeApprovedIndex) continue;
#endif
        const auto& info = deviceInfos[i];''')
args.output.write_text(text)
