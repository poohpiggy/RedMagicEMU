#!/usr/bin/env python3
"""Apply the AndroidPipe shared-memory experiment to the paired host source."""

from pathlib import Path
import re
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
#include <android/log.h>
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

    static void restrictBlobMemoryTypes(VkMemoryRequirements* requirements,
                                        const EmulatedPhysicalDeviceMemoryProperties& translation,
                                        bool usesBlob) {
        if (!sharedMemoryOwner() || !rme::sharedMemoryUsesHardwareBuffer()) return;
        const auto blobType = sharedMemoryType(translation.getHostMemoryProperties());
        if (blobType != UINT32_MAX) {
            const uint32_t mask = uint32_t{1} << blobType;
            requirements->memoryTypeBits &= usesBlob ? mask : ~mask;
        }
    }

    static VkResult allocateSharedBlob(VulkanDispatch* vk, VkDevice device,
                                       VkMemoryAllocateInfo* alloc, AHardwareBuffer** out) {
        if (!alloc->allocationSize || alloc->allocationSize > UINT32_MAX) {
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        auto getProperties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            vk->vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
        if (!getProperties) return VK_ERROR_EXTENSION_NOT_PRESENT;
        AHardwareBuffer_Desc desc{};
        desc.width = static_cast<uint32_t>(alloc->allocationSize);
        desc.height = 1;
        desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER | AHARDWAREBUFFER_USAGE_CPU_READ_RARELY |
                     AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
        if (AHardwareBuffer_allocate(&desc, out) != 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        VkAndroidHardwareBufferPropertiesANDROID props{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
        VkResult result = getProperties(device, *out, &props);
        if (result != VK_SUCCESS) return result;
        if (props.allocationSize < alloc->allocationSize ||
            !(props.memoryTypeBits & (uint32_t{1} << alloc->memoryTypeIndex))) {
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }
        alloc->allocationSize = props.allocationSize;
        return VK_SUCCESS;
    }

    static bool publishSharedBlob(AHardwareBuffer* buffer, VkDeviceSize size,
                                  uint64_t owner, uint64_t boxed,
                                  const void* coherentMapping = nullptr) {
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(buffer, &desc);
        using GetHandle = const native_handle_t* (*)(const AHardwareBuffer*);
        static const auto getHandle = reinterpret_cast<GetHandle>(
            dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle"));
        const auto* handle = getHandle ? getHandle(buffer) : nullptr;
        if (desc.format != AHARDWAREBUFFER_FORMAT_BLOB || desc.width < size ||
            desc.height != 1 || desc.layers != 1 ||
            !(desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER) ||
            !handle || handle->numFds <= 0) {
            return false;
        }
        if (coherentMapping) {
            const int fd = rme::sharedBlobDataFd(coherentMapping, size, handle->data, handle->numFds);
            if (fd >= 0) return rme::publishSharedMemory(owner, boxed, fd, true);
        }
        // Native handles are vendor-defined. Match the CPU mapping of this
        // newly allocated BLOB before handing a descriptor to the Guest.
        void* address = nullptr;
        const int locked = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,
                                                -1, nullptr, &address);
        const int fd = locked == 0
            ? rme::sharedBlobDataFd(address, size, handle->data, handle->numFds) : -1;
        const int unlocked = locked == 0 ? AHardwareBuffer_unlock(buffer, nullptr) : 0;
        if (fd < 0 || unlocked != 0) {
            __android_log_print(ANDROID_LOG_WARN, "RedMagicEMU-Vulkan",
                                "Shared BLOB mapping failed (fds=%d ints=%d lock=%d unlock=%d)",
                                handle->numFds, handle->numInts, locked, unlocked);
            return false;
        }
        return rme::publishSharedMemory(owner, boxed, fd);
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
            const auto& memory = props.externalMemoryProperties;
            if ((memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) &&
                !(memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) &&
                (memory.compatibleHandleTypes & request.handleType)) {
                localCreateInfo = *pCreateInfo;
                external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO, localCreateInfo.pNext,
                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
                localCreateInfo.pNext = &external;
                pCreateInfo = &localCreateInfo;
            }
        }
        VkResult result = vk->vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);''')

replace('            bufInfo.size = pCreateInfo->size;', '''            bufInfo.size = pCreateInfo->size;
            if (external.handleTypes) mSharedBlobBuffers.insert(*pBuffer);''')
replace('        mBufferInfo.clear();', '''        mBufferInfo.clear();
        mSharedBlobBuffers.clear();''')
text, cleanup_count = re.subn(r'^( +)mBufferInfo\.erase\(buffer\);$',
                             r'\1mSharedBlobBuffers.erase(buffer);\n\1mBufferInfo.erase(buffer);',
                             text, flags=re.MULTILINE)
if cleanup_count != 2:
    raise ValueError('unexpected buffer cleanup paths')
replace('    std::unordered_map<VkBuffer, BufferInfo> mBufferInfo;',
        '''    std::unordered_map<VkBuffer, BufferInfo> mBufferInfo;
    std::unordered_set<VkBuffer> mSharedBlobBuffers;''')

for name, next_name, req, buffer in (
    ('on_vkGetImageMemoryRequirements', 'on_vkGetImageMemoryRequirements2', 'pMemoryRequirements', None),
    ('on_vkGetImageMemoryRequirements2', 'on_vkGetBufferMemoryRequirements', '&pMemoryRequirements->memoryRequirements', None),
    ('on_vkGetBufferMemoryRequirements', 'on_vkGetBufferMemoryRequirements2', 'pMemoryRequirements', 'buffer'),
    ('on_vkGetBufferMemoryRequirements2', 'on_vkCmdCopyBufferToImage', '&pMemoryRequirements->memoryRequirements', 'pInfo->buffer'),
):
    start = text.index(f'    void {name}(')
    end = text.index(f'    void {next_name}(', start)
    head, method, tail = text[:start], text[start:end], text[end:]
    anchor = '        physicalDeviceMemHelper->transformToGuestMemoryRequirements('
    if method.count(anchor) != 1:
        raise ValueError('unexpected memory requirements near ' + name)
    uses_blob = f'mSharedBlobBuffers.count({buffer}) != 0' if buffer else 'false'
    text = head + method.replace(anchor, f'        restrictBlobMemoryTypes({req}, *physicalDeviceMemHelper,\n'
                                         f'                                {uses_blob});\n' + anchor) + tail

replace('        VkResult result = vk->vkAllocateMemory(device, &localAllocInfo, pAllocator, pMemory);',
        '''        const uint64_t sharedOwner = sharedMemoryOwner();
        const bool cachedShare = sharedOwner && hostVisible && !rme::sharedMemoryUsesHardwareBuffer() &&
                                 !importCbInfoPtr && !createBlobInfoPtr;
        const VkDeviceSize guestSize = localAllocInfo.allocationSize;
        bool coherentBlob = false;
        std::unique_ptr<AHardwareBuffer, decltype(&AHardwareBuffer_release)> blob(nullptr, AHardwareBuffer_release);
        VkImportAndroidHardwareBufferInfoANDROID importBlob{
            VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
        if (sharedOwner && hostVisible && rme::sharedMemoryUsesHardwareBuffer() &&
            !importCbInfoPtr && !importBufferInfoPtr && !createBlobInfoPtr) {
            std::lock_guard<std::recursive_mutex> lock(mLock);
            auto physical = android::base::find(mDeviceToPhysicalDevice, device);
            if (!physical) return VK_ERROR_INITIALIZATION_FAILED;
            VkPhysicalDeviceMemoryProperties props{};
            m_vk->vkGetPhysicalDeviceMemoryProperties(*physical, &props);
            if (localAllocInfo.memoryTypeIndex == sharedMemoryType(props)) {
                if ((dedicatedAllocInfoPtr && dedicatedAllocInfoPtr->image) || mappedPtr ||
                    m_emu->features.ExternalBlob.enabled) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
                AHardwareBuffer* buffer = nullptr;
                const VkResult allocated = allocateSharedBlob(vk, device, &localAllocInfo, &buffer);
                blob.reset(buffer);
                if (allocated != VK_SUCCESS) return allocated;
#ifdef RME_VULKAN_COHERENT_AHB_TEST
                // Temporary diagnostic build for the measured Adreno driver.
                // Qualify the actual Vulkan mapping again for each allocation.
                VkPhysicalDeviceProperties nativeProps{};
                m_vk->vkGetPhysicalDeviceProperties(*physical, &nativeProps);
                coherentBlob = nativeProps.vendorID == 0x5143 &&
                               nativeProps.deviceID == 0x43051401 &&
                               nativeProps.driverVersion == 0x802fa031;
#endif
                importBlob.buffer = buffer;
                importBlob.pNext = localAllocInfo.pNext;
                localAllocInfo.pNext = &importBlob;
            }
        }
        VkResult result = vk->vkAllocateMemory(device, &localAllocInfo, pAllocator, pMemory);''')

replace('''        *pMemory = new_boxed_non_dispatchable_VkDeviceMemory(*pMemory);

        return result;
    }

    void freeMemoryLocked''',
        '''        const VkDeviceMemory nativeMemory = *pMemory;
        *pMemory = new_boxed_non_dispatchable_VkDeviceMemory(nativeMemory);
        if (cachedShare || blob) {
            const uint64_t boxed = (uint64_t)*pMemory;
            const bool published = cachedShare
                ? rme::publishCachedSharedMemory(sharedOwner, boxed, memoryInfo.ptr, memoryInfo.size)
                : publishSharedBlob(blob.get(), guestSize, sharedOwner, boxed,
                                    coherentBlob ? memoryInfo.ptr : nullptr);
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
