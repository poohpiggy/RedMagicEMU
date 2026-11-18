#!/usr/bin/env python3
"""Apply the AndroidPipe shared-memory experiment to the paired host source."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text()


def replace(old, new):
    global text
    if text.count(old) != 1:
        raise ValueError("unexpected decoder source near " + old[:80])
    text = text.replace(old, new, 1)


replace('#include "VkDecoderGlobalState.h"', '''#include "VkDecoderGlobalState.h"

#include <android/hardware_buffer.h>
#include <cutils/native_handle.h>
#include <dlfcn.h>
#include <unistd.h>
#include <vulkan/vulkan_android.h>

#include "RenderThreadInfo.h"
#include "shared_memory.h"

uint64_t rme::currentSharedMemoryOwner() {
    if (!sharedMemoryEnabled()) return 0;
    const auto* thread = gfxstream::RenderThreadInfo::get();
    return thread ? thread->m_puid : 0;
}
''')

replace('    void on_vkGetPhysicalDeviceMemoryProperties(\n', '''    static uint64_t sharedMemoryOwner() {
        return rme::currentSharedMemoryOwner();
    }

    static uint32_t sharedMemoryType(const VkPhysicalDeviceMemoryProperties& props) {
        constexpr auto flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if (props.memoryTypes[i].propertyFlags == flags) return i;
        }
        return UINT32_MAX;
    }

    static void describeSharedMemoryTypes(VkPhysicalDeviceMemoryProperties* props,
                                          const EmulatedPhysicalDeviceMemoryProperties& translation) {
        if (!sharedMemoryOwner()) return;
        const bool useHardwareBuffer = rme::sharedMemoryUsesHardwareBuffer();
        const auto mapped = sharedMemoryType(translation.getHostMemoryProperties());
        for (uint32_t i = 0; i < props->memoryTypeCount; ++i) {
            const auto host = translation.getHostMemoryInfoFromGuestMemoryTypeIndex(i);
            auto& flags = props->memoryTypes[i].propertyFlags;
            if (!useHardwareBuffer && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                // The guest maps CPU-cached shared pages. Explicit flush/invalidate
                // synchronize those pages with the driver's native mapped allocation.
                flags = (flags & ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) |
                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            } else if (useHardwareBuffer && host && host->index == mapped &&
                       (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                flags |= host->memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            } else {
                flags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            }
        }
    }

    static bool exportSharedMemory(VulkanDispatch* vk, VkDevice device, VkDeviceMemory native,
                                   VkDeviceSize size, uint64_t owner, uint64_t boxed) {
        auto exportBuffer = reinterpret_cast<PFN_vkGetMemoryAndroidHardwareBufferANDROID>(
            vk->vkGetDeviceProcAddr(device, "vkGetMemoryAndroidHardwareBufferANDROID"));
        if (!exportBuffer) return false;
        VkMemoryGetAndroidHardwareBufferInfoANDROID request{
            VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, nullptr, native};
        AHardwareBuffer* buffer = nullptr;
        if (exportBuffer(device, &request, &buffer) != VK_SUCCESS || !buffer) return false;
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(buffer, &desc);
        using GetHandle = const native_handle_t* (*)(const AHardwareBuffer*);
        static const auto getHandle = reinterpret_cast<GetHandle>(
            dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle"));
        const auto* handle = getHandle ? getHandle(buffer) : nullptr;
        // The validated Adreno BLOB layout contains data and metadata fds.
        bool published = false;
        if (desc.format == AHARDWAREBUFFER_FORMAT_BLOB && desc.width == size &&
            desc.height == 1 && desc.layers == 1 &&
            (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER) &&
            handle && handle->numFds == 2 && handle->numInts == 26) {
            const auto bytes = lseek(handle->data[0], 0, SEEK_END);
            if (bytes > 0 && static_cast<VkDeviceSize>(bytes) >= size) {
                lseek(handle->data[0], 0, SEEK_SET);
                published = rme::publishSharedMemory(owner, boxed, handle->data[0]);
            }
        }
        AHardwareBuffer_release(buffer);
        return published;
    }

    void on_vkGetPhysicalDeviceMemoryProperties(
''')
replace('        *pMemoryProperties = physicalDeviceMemoryHelper->getGuestMemoryProperties();',
        '''        *pMemoryProperties = physicalDeviceMemoryHelper->getGuestMemoryProperties();
        describeSharedMemoryTypes(pMemoryProperties, *physicalDeviceMemoryHelper);''')
replace('''        pMemoryProperties->memoryProperties =
            physicalDeviceMemoryHelper->getGuestMemoryProperties();''',
        '''        pMemoryProperties->memoryProperties =
            physicalDeviceMemoryHelper->getGuestMemoryProperties();
        describeSharedMemoryTypes(&pMemoryProperties->memoryProperties, *physicalDeviceMemoryHelper);''')

replace('''        const std::vector<const char*> finalExts =
            filteredDeviceExtensionNames(vk, physicalDevice, pCreateInfo->enabledExtensionCount,
                                         pCreateInfo->ppEnabledExtensionNames);''',
        '''        auto finalExts = filteredDeviceExtensionNames(
            vk, physicalDevice, pCreateInfo->enabledExtensionCount, pCreateInfo->ppEnabledExtensionNames);
        if (sharedMemoryOwner() && rme::sharedMemoryUsesHardwareBuffer()) {
            std::vector<VkExtensionProperties> extensions;
            if (enumerateDeviceExtensionProperties(vk, physicalDevice, nullptr, extensions) != VK_SUCCESS ||
                !hasDeviceExtension(extensions, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) ||
                !hasDeviceExtension(extensions, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
            for (const char* name : {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                                     VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME}) {
                const auto enabled = std::find_if(finalExts.begin(), finalExts.end(),
                    [name](const char* entry) { return std::strcmp(entry, name) == 0; });
                if (enabled == finalExts.end()) finalExts.push_back(name);
            }
        }''')

replace('        VkResult result = vk->vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);',
        '''        VkExternalMemoryBufferCreateInfo external{};
        if (sharedMemoryOwner() && rme::sharedMemoryUsesHardwareBuffer() &&
            !vk_find_struct<VkExternalMemoryBufferCreateInfo>(pCreateInfo)) {
            std::lock_guard<std::recursive_mutex> lock(mLock);
            auto physical = android::base::find(mDeviceToPhysicalDevice, device);
            if (!physical) return VK_ERROR_INITIALIZATION_FAILED;
            VkPhysicalDeviceExternalBufferInfo request{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO, nullptr, pCreateInfo->flags,
                pCreateInfo->usage, VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
            VkExternalBufferProperties props{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
            m_vk->vkGetPhysicalDeviceExternalBufferProperties(*physical, &request, &props);
            if (props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) {
                localCreateInfo = *pCreateInfo;
                external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, localCreateInfo.pNext,
                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
                localCreateInfo.pNext = &external;
                pCreateInfo = &localCreateInfo;
            }
        }
        VkResult result = vk->vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);''')

replace('        VkResult result = vk->vkAllocateMemory(device, &localAllocInfo, pAllocator, pMemory);',
        '''        const uint64_t sharedOwner = sharedMemoryOwner();
        const bool cachedShare = sharedOwner && hostVisible && !rme::sharedMemoryUsesHardwareBuffer() &&
                                 !importCbInfoPtr && !createBlobInfoPtr;
        bool share = cachedShare;
        if (sharedOwner && hostVisible && rme::sharedMemoryUsesHardwareBuffer()) {
            std::lock_guard<std::recursive_mutex> lock(mLock);
            auto physical = android::base::find(mDeviceToPhysicalDevice, device);
            if (!physical) return VK_ERROR_INITIALIZATION_FAILED;
            VkPhysicalDeviceMemoryProperties props{};
            m_vk->vkGetPhysicalDeviceMemoryProperties(*physical, &props);
            share = localAllocInfo.memoryTypeIndex == sharedMemoryType(props);
            if (share) {
                exportAllocate.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
                vk_append_struct(&structChainIter, &exportAllocate);
            }
        }
        VkResult result = vk->vkAllocateMemory(device, &localAllocInfo, pAllocator, pMemory);''')

replace('''        *pMemory = new_boxed_non_dispatchable_VkDeviceMemory(*pMemory);

        return result;
    }

    void freeMemoryLocked''',
        '''        const VkDeviceMemory nativeMemory = *pMemory;
        *pMemory = new_boxed_non_dispatchable_VkDeviceMemory(nativeMemory);
        if (share) {
            const uint64_t boxed = (uint64_t)*pMemory;
            const bool published = cachedShare
                ? rme::publishCachedSharedMemory(sharedOwner, boxed, memoryInfo.ptr, memoryInfo.size)
                : exportSharedMemory(vk, device, nativeMemory, memoryInfo.size, sharedOwner, boxed);
            if (!published) {
                freeMemoryLocked(vk, device, nativeMemory, pAllocator);
                delete_VkDeviceMemory(*pMemory);
                *pMemory = VK_NULL_HANDLE;
                return VK_ERROR_MEMORY_MAP_FAILED;
            }
            mSharedMemoryIds.emplace(nativeMemory, std::make_pair(sharedOwner, boxed));
        }
        return result;
    }

    void freeMemoryLocked''')
replace('        vk->vkFreeMemory(device, memory, pAllocator);\n\n        mMemoryInfo.erase(memory);',
        '''        const auto shared = mSharedMemoryIds.find(memory);
        if (shared != mSharedMemoryIds.end()) {
            rme::revokeSharedMemory(shared->second.first, shared->second.second);
            mSharedMemoryIds.erase(shared);
        }
        vk->vkFreeMemory(device, memory, pAllocator);

        mMemoryInfo.erase(memory);''')
replace('    std::unordered_map<VkDeviceMemory, MemoryInfo> mMemoryInfo;',
        '''    std::unordered_map<VkDeviceMemory, MemoryInfo> mMemoryInfo;
    std::unordered_map<VkDeviceMemory, std::pair<uint64_t, uint64_t>> mSharedMemoryIds;''')
replace('''    bool usingDirectMapping() const {
        return m_emu->features.GlDirectMem.enabled ||''',
        '''    bool usingDirectMapping() const {
        return sharedMemoryOwner() || m_emu->features.GlDirectMem.enabled ||''')

output.write_text(text)
