#!/usr/bin/env python3
"""Generate a candidate on top of the d7a swapchain metadata patch."""
import difflib
from pathlib import Path
import subprocess
import sys
import tempfile

source, output = map(Path, sys.argv[1:])
names = ['host/vulkan/VkDecoderGlobalState.cpp', 'host/vulkan/VkDecoderGlobalState.h', 'host/vulkan/VkDecoder.cpp']
metadata = Path(__file__).resolve().parent.parent / 'gfxstream-swapchain-metadata/fix.patch'
with tempfile.TemporaryDirectory(prefix='rme-swapchain-images-gen-') as temp:
    root = Path(temp)
    for name in names:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / name).read_bytes())
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(metadata)], cwd=root, check=True)
    original = {name: (root / name).read_text() for name in names}
changed = dict(original)

def replace(name, before, after):
    if changed[name].count(before) != 1:
        raise ValueError(f'{name}: expected one exact occurrence of {before!r}')
    changed[name] = changed[name].replace(before, after, 1)

state, header, decoder = names
replace(state, '#include <unordered_map>\n', '#include <unordered_map>\n#include <unordered_set>\n')
replace(state, '        std::vector<uint32_t> queueFamilyIndices;\n',
        '        std::vector<uint32_t> queueFamilyIndices;\n        std::unordered_map<VkImage, VkImage> images;\n')
replace(state, '            info->imageSharingMode, info->pNext != nullptr, {}};',
        '            info->imageSharingMode, info->pNext != nullptr, {}, {}};')
anchor = '    void on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed) {\n'
methods = '''    VkResult on_validateSwapchain(VkDevice device, VkSwapchainKHR boxed) {
        std::lock_guard<std::recursive_mutex> lock(mLock);
        auto it = mSwapchainImageMetadata.find(boxed);
        auto* deviceInfo = android::base::find(mDeviceInfo, device);
        if (it == mSwapchainImageMetadata.end() || it->second.device != device ||
            it->second.hasPNext || it->second.flags || snapshotsEnabled() || !deviceInfo ||
            deviceInfo->needEmulatedDecompression(it->second.format)) return VK_ERROR_UNKNOWN;
        return VK_SUCCESS;
    }

    VkResult on_registerSwapchainImages(VkDevice device, VkSwapchainKHR boxed,
                                        uint32_t count, VkImage* images) {
        std::lock_guard<std::recursive_mutex> lock(mLock);
        if (on_validateSwapchain(device, boxed) != VK_SUCCESS || (count && !images))
            return VK_ERROR_UNKNOWN;
        auto& meta = mSwapchainImageMetadata.at(boxed);
        std::unordered_set<VkImage> seen;
        for (uint32_t i = 0; i < count; ++i) {
            VkImage raw = images[i];
            if (!raw || !seen.insert(raw).second) return VK_ERROR_UNKNOWN;
            auto known = meta.images.find(raw);
            auto owner = mSwapchainImageOwners.find(raw);
            auto imageInfo = mImageInfo.find(raw);
            VkImage reverse = unboxed_to_boxed_non_dispatchable_VkImage(raw);
            if (known != meta.images.end()) {
                if (owner == mSwapchainImageOwners.end() || owner->second != boxed ||
                    imageInfo == mImageInfo.end() || imageInfo->second.device != device ||
                    reverse != known->second) return VK_ERROR_UNKNOWN;
            } else if (owner != mSwapchainImageOwners.end() || imageInfo != mImageInfo.end() || reverse) {
                return VK_ERROR_UNKNOWN;
            }
        }
        std::vector<VkImage> added;
        for (uint32_t i = 0; i < count; ++i) {
            VkImage raw = images[i];
            if (meta.images.find(raw) != meta.images.end()) continue;
            VkImage imageBox = new_boxed_non_dispatchable_VkImage(raw);
            if (!imageBox) {
                for (VkImage prior : added) {
                    delete_VkImage(meta.images.at(prior));
                    meta.images.erase(prior);
                    mSwapchainImageOwners.erase(prior);
                    mImageInfo.erase(prior);
                }
                return VK_ERROR_UNKNOWN;
            }
            meta.images[raw] = imageBox;
            mSwapchainImageOwners[raw] = boxed;
            auto& imageInfo = mImageInfo[raw];
            imageInfo.device = device;
            imageInfo.cmpInfo = CompressedImageInfo(device);
            VkImageCreateInfo ci = {};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = meta.format;
            ci.extent = {meta.extent.width, meta.extent.height, 1};
            ci.mipLevels = 1;
            ci.arrayLayers = meta.arrayLayers;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            ci.usage = meta.usage;
            ci.sharingMode = meta.sharingMode;
            ci.queueFamilyIndexCount = static_cast<uint32_t>(meta.queueFamilyIndices.size());
            ci.pQueueFamilyIndices = meta.queueFamilyIndices.empty() ? nullptr : meta.queueFamilyIndices.data();
            ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.imageCreateInfoShallow = ci;
            imageInfo.layout = ci.initialLayout;
            added.push_back(raw);
        }
        for (uint32_t i = 0; i < count; ++i) images[i] = meta.images.at(images[i]);
        return VK_SUCCESS;
    }

    // No vkDestroyImage: the swapchain/driver owns these images.
    void releaseSwapchainImagesLocked(VkSwapchainKHR boxed, SwapchainImageMetadata& meta) {
        for (const auto& entry : meta.images) {
            auto owner = mSwapchainImageOwners.find(entry.first);
            if (owner != mSwapchainImageOwners.end() && owner->second == boxed) {
                mImageInfo.erase(entry.first);
                mSwapchainImageOwners.erase(owner);
            }
            delete_VkImage(entry.second);
        }
        meta.images.clear();
    }

'''
replace(state, anchor, methods + anchor)
old = '''        if (it != mSwapchainImageMetadata.end() && it->second.device == device)
            mSwapchainImageMetadata.erase(it);
'''
new = '''        if (it != mSwapchainImageMetadata.end() && it->second.device == device) {
            releaseSwapchainImagesLocked(boxed, it->second);
            mSwapchainImageMetadata.erase(it);
        }
'''
replace(state, old, new)
old = '            if (it->second.device == device) it = mSwapchainImageMetadata.erase(it);\n'
new = '''            if (it->second.device == device) {
                releaseSwapchainImagesLocked(it->first, it->second);
                it = mSwapchainImageMetadata.erase(it);
            }
'''
replace(state, old, new)
old = '        mImageInfo.clear();\n        mSwapchainImageMetadata.clear();\n'
new = '''        for (auto& entry : mSwapchainImageMetadata)
            releaseSwapchainImagesLocked(entry.first, entry.second);
        mSwapchainImageOwners.clear();
        mImageInfo.clear();
        mSwapchainImageMetadata.clear();
'''
replace(state, old, new)
old = '''        auto* imageInfo = android::base::find(mImageInfo, image);
        if (!imageInfo) return;

        if (!imageInfo->anbInfo) {
'''
new = '''        auto* imageInfo = android::base::find(mImageInfo, image);
        if (!imageInfo || mSwapchainImageOwners.count(image)) return;

        if (!imageInfo->anbInfo) {
'''
replace(state, old, new)
old = '    std::unordered_map<VkSwapchainKHR, SwapchainImageMetadata> mSwapchainImageMetadata;\n'
replace(state, old, old + '    std::unordered_map<VkImage, VkSwapchainKHR> mSwapchainImageOwners;\n')
anchor = 'void VkDecoderGlobalState::on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed) {\n'
wrappers = '''VkResult VkDecoderGlobalState::on_validateSwapchain(VkDevice device, VkSwapchainKHR boxed) {
    return mImpl->on_validateSwapchain(device, boxed);
}

VkResult VkDecoderGlobalState::on_registerSwapchainImages(VkDevice device, VkSwapchainKHR boxed,
                                                           uint32_t count, VkImage* images) {
    return mImpl->on_registerSwapchainImages(device, boxed, count, images);
}

'''
replace(state, anchor, wrappers + anchor)
anchor = '    void on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed);\n'
decls = '''    VkResult on_validateSwapchain(VkDevice device, VkSwapchainKHR boxed);
    VkResult on_registerSwapchainImages(VkDevice device, VkSwapchainKHR boxed,
                                        uint32_t count, VkImage* images);
'''
replace(header, anchor, decls + anchor)
start = changed[decoder].index('            case OP_vkGetSwapchainImagesKHR:')
end = changed[decoder].index('            case OP_vkAcquireNextImageKHR:', start)
section = changed[decoder][start:end]
old = '''                *(VkSwapchainKHR*)&swapchain =
                    (VkSwapchainKHR)unbox_VkSwapchainKHR((VkSwapchainKHR)(*&cgen_var_1));
'''
new = '''                VkSwapchainKHR boxed_swapchain = (VkSwapchainKHR)(*&cgen_var_1);
                swapchain = unbox_VkSwapchainKHR(boxed_swapchain);
'''
if section.count(old) != 1: raise ValueError('unexpected swapchain unbox')
section = section.replace(old, new, 1)
old = '''                VkResult vkGetSwapchainImagesKHR_VkResult_return = (VkResult)0;
                vkGetSwapchainImagesKHR_VkResult_return = vk->vkGetSwapchainImagesKHR(
                    unboxed_device, swapchain, pSwapchainImageCount, pSwapchainImages);
'''
new = '''                VkResult vkGetSwapchainImagesKHR_VkResult_return =
                    m_state->on_validateSwapchain(unboxed_device, boxed_swapchain);
                if (vkGetSwapchainImagesKHR_VkResult_return == VK_SUCCESS) {
                    vkGetSwapchainImagesKHR_VkResult_return = vk->vkGetSwapchainImagesKHR(
                        unboxed_device, swapchain, pSwapchainImageCount, pSwapchainImages);
                }
                if ((vkGetSwapchainImagesKHR_VkResult_return == VK_SUCCESS ||
                     vkGetSwapchainImagesKHR_VkResult_return == VK_INCOMPLETE) && pSwapchainImages) {
                    VkResult registration = m_state->on_registerSwapchainImages(
                        unboxed_device, boxed_swapchain, *pSwapchainImageCount, pSwapchainImages);
                    if (registration != VK_SUCCESS) vkGetSwapchainImagesKHR_VkResult_return = registration;
                }
                if (vkGetSwapchainImagesKHR_VkResult_return != VK_SUCCESS &&
                    vkGetSwapchainImagesKHR_VkResult_return != VK_INCOMPLETE &&
                    pSwapchainImages && pSwapchainImageCount) *pSwapchainImageCount = 0;
'''
if section.count(old) != 1: raise ValueError('unexpected enumeration dispatch')
section = section.replace(old, new, 1)
changed[decoder] = changed[decoder][:start] + section + changed[decoder][end:]
diff = []
for name in names:
    diff.extend(difflib.unified_diff(original[name].splitlines(True), changed[name].splitlines(True),
                                     fromfile='a/' + name, tofile='b/' + name))
output.write_text(''.join(diff))
