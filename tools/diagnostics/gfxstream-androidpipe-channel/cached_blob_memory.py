#!/usr/bin/env python3
"""Expose the cacheability of the verified direct Android BLOB mapping."""

from pathlib import Path
import sys


def apply(source):
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError("unexpected decoder source near " + old[:90])
        source = source.replace(old, new, 1)

    replace('''    static void describeSharedMemoryTypes(VkPhysicalDeviceMemoryProperties* props,
                                          const EmulatedPhysicalDeviceMemoryProperties& translation) {''',
            '''    static bool hasVerifiedCoherentBlobMapping(const VkPhysicalDeviceProperties& props) {
#ifdef RME_VULKAN_COHERENT_AHB_TEST
        // The exported BLOB mapping for this driver is coherent and CPU-cached.
        return props.vendorID == 0x5143 && props.deviceID == 0x43051401 &&
               props.driverVersion == 0x802fa031;
#else
        (void)props;
        return false;
#endif
    }

    static void describeSharedMemoryTypes(VkPhysicalDeviceMemoryProperties* props,
                                          const EmulatedPhysicalDeviceMemoryProperties& translation,
                                          const VkPhysicalDeviceProperties& nativeProps) {''')
    replace('''                flags |= host->memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;''',
            '''                flags |= host->memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                if (hasVerifiedCoherentBlobMapping(nativeProps)) flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;''')
    replace('''        describeSharedMemoryTypes(pMemoryProperties, *physicalDeviceMemoryHelper);''',
            '''        describeSharedMemoryTypes(pMemoryProperties, *physicalDeviceMemoryHelper,
                                  physicalDeviceInfo->props);''')
    replace('''        describeSharedMemoryTypes(&pMemoryProperties->memoryProperties, *physicalDeviceMemoryHelper);''',
            '''        describeSharedMemoryTypes(&pMemoryProperties->memoryProperties, *physicalDeviceMemoryHelper,
                                  physicalDeviceInfo->props);''')
    replace('''#ifdef RME_VULKAN_COHERENT_AHB_TEST
                // Temporary diagnostic build for the measured Adreno driver.
                // Qualify the actual Vulkan mapping again for each allocation.
                VkPhysicalDeviceProperties nativeProps{};
                m_vk->vkGetPhysicalDeviceProperties(*physical, &nativeProps);
                coherentBlob = nativeProps.vendorID == 0x5143 &&
                               nativeProps.deviceID == 0x43051401 &&
                               nativeProps.driverVersion == 0x802fa031;
#endif''',
            '''#ifdef RME_VULKAN_COHERENT_AHB_TEST
                VkPhysicalDeviceProperties nativeProps{};
                m_vk->vkGetPhysicalDeviceProperties(*physical, &nativeProps);
                coherentBlob = hasVerifiedCoherentBlobMapping(nativeProps);
#endif''')
    return source


if __name__ == "__main__":
    original, output = map(Path, sys.argv[1:])
    output.write_text(apply(original.read_text()))
