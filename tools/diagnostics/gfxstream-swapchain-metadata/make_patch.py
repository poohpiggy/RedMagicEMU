#!/usr/bin/env python3
"""Generate an isolated metadata patch from the checked local d7a source."""
import difflib
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
names = ['host/vulkan/VkDecoderGlobalState.cpp', 'host/vulkan/VkDecoderGlobalState.h', 'host/vulkan/VkDecoder.cpp']
original = {name: (source / name).read_text() for name in names}
changed = dict(original)

def replace(name, before, after):
    if changed[name].count(before) != 1:
        raise ValueError(f'{name}: expected one exact occurrence of {before!r}')
    changed[name] = changed[name].replace(before, after, 1)

state, header, decoder = names
methods = '''    // RME swapchain metadata begin
    struct SwapchainImageMetadata {
        VkDevice device;
        VkSwapchainCreateFlagsKHR flags;
        VkFormat format;
        VkExtent2D extent;
        uint32_t arrayLayers;
        VkImageUsageFlags usage;
        VkSharingMode sharingMode;
        bool hasPNext;
        std::vector<uint32_t> queueFamilyIndices;
    };

    void on_recordSwapchain(VkDevice device, VkSwapchainKHR boxed,
                            const VkSwapchainCreateInfoKHR* info) {
        SwapchainImageMetadata saved{device, info->flags, info->imageFormat,
            info->imageExtent, info->imageArrayLayers, info->imageUsage,
            info->imageSharingMode, info->pNext != nullptr, {}};
        if (info->imageSharingMode == VK_SHARING_MODE_CONCURRENT && info->queueFamilyIndexCount) {
            saved.queueFamilyIndices.assign(info->pQueueFamilyIndices,
                                           info->pQueueFamilyIndices + info->queueFamilyIndexCount);
        }
        std::lock_guard<std::recursive_mutex> lock(mLock);
        mSwapchainImageMetadata[boxed] = std::move(saved);
    }

    void on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed) {
        std::lock_guard<std::recursive_mutex> lock(mLock);
        auto it = mSwapchainImageMetadata.find(boxed);
        if (it != mSwapchainImageMetadata.end() && it->second.device == device)
            mSwapchainImageMetadata.erase(it);
    }

    // Caller holds mLock, as in destroyDeviceLocked.
    void forgetDeviceSwapchainsLocked(VkDevice device) {
        for (auto it = mSwapchainImageMetadata.begin(); it != mSwapchainImageMetadata.end();) {
            if (it->second.device == device) it = mSwapchainImageMetadata.erase(it);
            else ++it;
        }
    }
    // RME swapchain metadata end

'''
needle = '    VkResult on_vkCreateImage(android::base::BumpPool* pool, VkDevice boxed_device,'
replace(state, needle, methods + needle)
replace(state, '        mImageInfo.clear();\n', '        mImageInfo.clear();\n        mSwapchainImageMetadata.clear();\n')
needle = '    void destroyDeviceLocked(VkDevice device, const VkAllocationCallbacks* pAllocator) {\n'
replace(state, needle, needle + '        forgetDeviceSwapchainsLocked(device);\n')
needle = '    std::unordered_map<VkImage, ImageInfo> mImageInfo;\n'
replace(state, needle, needle + '    std::unordered_map<VkSwapchainKHR, SwapchainImageMetadata> mSwapchainImageMetadata;\n')
needle = 'VkResult VkDecoderGlobalState::on_vkCreateImage(android::base::BumpPool* pool, VkDevice device,\n'
wrappers = '''void VkDecoderGlobalState::on_recordSwapchain(VkDevice device, VkSwapchainKHR boxed,
                                              const VkSwapchainCreateInfoKHR* info) {
    mImpl->on_recordSwapchain(device, boxed, info);
}

void VkDecoderGlobalState::on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed) {
    mImpl->on_forgetSwapchain(device, boxed);
}

'''
replace(state, needle, wrappers + needle)
needle = '    VkResult on_vkCreateImage(android::base::BumpPool* pool, VkDevice device,\n'
decl = '''    void on_recordSwapchain(VkDevice device, VkSwapchainKHR boxed,
                            const VkSwapchainCreateInfoKHR* info);
    void on_forgetSwapchain(VkDevice device, VkSwapchainKHR boxed);

'''
replace(header, needle, decl + needle)
needle = '''                vkStream->handleMapping()->mapHandles_VkSwapchainKHR((VkSwapchainKHR*)pSwapchain,
                                                                     1);
                vkStream->write((VkSwapchainKHR*)pSwapchain, 8 * 1);
'''
replace(decoder, needle, '''                vkStream->handleMapping()->mapHandles_VkSwapchainKHR((VkSwapchainKHR*)pSwapchain,
                                                                     1);
                if (vkCreateSwapchainKHR_VkResult_return == VK_SUCCESS)
                    m_state->on_recordSwapchain(unboxed_device, *pSwapchain, pCreateInfo);
                vkStream->write((VkSwapchainKHR*)pSwapchain, 8 * 1);
''')
needle = '                vk->vkDestroySwapchainKHR(unboxed_device, swapchain, pAllocator);\n'
replace(decoder, needle, '                m_state->on_forgetSwapchain(unboxed_device, boxed_swapchain_preserve);\n' + needle)
diff = []
for name in names:
    diff.extend(difflib.unified_diff(original[name].splitlines(True), changed[name].splitlines(True),
                                     fromfile='a/' + name, tofile='b/' + name))
output.write_text(''.join(diff))
