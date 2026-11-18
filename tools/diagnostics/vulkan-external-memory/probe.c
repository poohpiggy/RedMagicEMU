// SPDX-License-Identifier: MIT
// Query-only: does not create devices, allocate memory, or import/export handles.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "capability.h"

typedef struct {
    const char *name;
    const char *extension;
    VkExternalMemoryHandleTypeFlagBits type;
} HandleKind;

static const HandleKind handles[] = {
    { "opaque_fd", "VK_KHR_external_memory_fd", VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT },
    { "dma_buf", "VK_EXT_external_memory_dma_buf", VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT },
    { "host_allocation", "VK_EXT_external_memory_host", VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT },
    { "android_hardware_buffer", "VK_ANDROID_external_memory_android_hardware_buffer",
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID },
};

static int has_extension(const VkExtensionProperties *extensions, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(extensions[i].extensionName, name) == 0) return 1;
    return 0;
}

static void report(const char *resource, const HandleKind *handle, VkExternalMemoryProperties p) {
    int can_import = external_memory_matches(1, p.externalMemoryFeatures, p.compatibleHandleTypes,
                                            handle->type, VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    int can_export = external_memory_matches(1, p.externalMemoryFeatures, p.compatibleHandleTypes,
                                            handle->type, VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
    int both = external_memory_matches(1, p.externalMemoryFeatures, p.compatibleHandleTypes,
                                       handle->type, VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT |
                                                         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
    printf("RESOURCE=%s HANDLE=%s RESULT=queried FEATURES=0x%08x COMPATIBLE=0x%08x "
           "EXPORT_FROM_IMPORTED=0x%08x IMPORT=%d EXPORT=%d BOTH=%d DEDICATED_ONLY=%d\n",
           resource, handle->name, p.externalMemoryFeatures, p.compatibleHandleTypes,
           p.exportFromImportedHandleTypes, can_import, can_export, both,
           !!(p.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT));
}

static void report_combination(const char *resource, const HandleProperties *all,
                               size_t first, size_t second) {
    HandleProperties members[] = { all[first], all[second] };
    uint32_t mask = members[0].handle | members[1].handle;
    CombinationState state = external_combination(members, 2, mask,
                                                   VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
    const char *result = state == COMBINATION_SUPPORTED ? "supported" :
                         state == COMBINATION_UNSUPPORTED ? "unsupported" : "unknown";
    printf("COMBINATION RESOURCE=%s MASK=0x%08x MEMBERS=%s|%s EXPORT=%s\n",
           resource, mask, handles[first].name, handles[second].name, result);
}

int main(void) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion version = (PFN_vkEnumerateInstanceVersion)
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (version && version(&loader_version) != VK_SUCCESS) {
        fputs("FAIL loader version query\n", stderr);
        return 1;
    }
    printf("LOADER_API=%u.%u.%u\n", VK_VERSION_MAJOR(loader_version),
           VK_VERSION_MINOR(loader_version), VK_VERSION_PATCH(loader_version));
    if (loader_version < VK_API_VERSION_1_1) {
        fputs("FAIL Vulkan 1.1 loader required for capability queries\n", stderr);
        return 1;
    }
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "rme-external-memory-probe",
                              .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&info, NULL, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "FAIL vkCreateInstance=%d\n", result);
        return 1;
    }
    PFN_vkGetPhysicalDeviceExternalBufferProperties get_buffer =
        (PFN_vkGetPhysicalDeviceExternalBufferProperties)
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceExternalBufferProperties");
    PFN_vkGetPhysicalDeviceImageFormatProperties2 get_image =
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceImageFormatProperties2");
    PFN_vkGetPhysicalDeviceProperties2 get_properties =
        (PFN_vkGetPhysicalDeviceProperties2)
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2");
    int failed = 0;
    uint32_t queried_devices = 0;
    VkPhysicalDevice *devices = NULL;
    if (!get_buffer || !get_image || !get_properties) {
        fputs("FAIL missing Vulkan 1.1 query functions\n", stderr);
        failed = 1;
        goto done;
    }
    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        fprintf(stderr, "FAIL vkEnumeratePhysicalDevices=%d count=%u\n", result, count);
        failed = 1;
        goto done;
    }
    devices = calloc(count, sizeof(*devices));
    if (!devices) { failed = 1; goto done; }
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "FAIL vkEnumeratePhysicalDevices(list)=%d\n", result);
        failed = 1;
        goto done;
    }
    puts("BUFFER flags=0 usage=STORAGE_BUFFER");
    puts("IMAGE format=R8G8B8A8_UNORM type=2D tiling=OPTIMAL "
         "usage=SAMPLED|COLOR_ATTACHMENT flags=0");
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        printf("DEVICE=%u NAME=%s TYPE=%u VENDOR=0x%04x DEVICE_ID=0x%04x "
               "API=%u.%u.%u DRIVER=0x%08x\n", i, props.deviceName, props.deviceType,
               props.vendorID, props.deviceID, VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion),
               props.driverVersion);
        if (props.apiVersion < VK_API_VERSION_1_1) {
            puts("DEVICE_RESULT=not_queried reason=physical_device_below_vulkan_1_1");
            continue;
        }
        uint32_t n = 0;
        result = vkEnumerateDeviceExtensionProperties(devices[i], NULL, &n, NULL);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "FAIL device=%u extension_count=%d\n", i, result);
            failed = 1;
            continue;
        }
        VkExtensionProperties *extensions = calloc(n ? n : 1, sizeof(*extensions));
        if (!extensions) { failed = 1; continue; }
        result = vkEnumerateDeviceExtensionProperties(devices[i], NULL, &n, extensions);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "FAIL device=%u extension_list=%d\n", i, result);
            free(extensions);
            failed = 1;
            continue;
        }
        ++queried_devices;
        if (has_extension(extensions, n, "VK_EXT_external_memory_host")) {
            VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT
            };
            VkPhysicalDeviceProperties2 properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host
            };
            get_properties(devices[i], &properties);
            printf("HOST_POINTER_MIN_ALIGNMENT=%" PRIu64 "\n",
                   (uint64_t)host.minImportedHostPointerAlignment);
        }
        HandleProperties buffer_queries[sizeof(handles) / sizeof(handles[0])] = {0};
        HandleProperties image_queries[sizeof(handles) / sizeof(handles[0])] = {0};
        for (size_t h = 0; h < sizeof(handles) / sizeof(handles[0]); ++h) {
            const HandleKind *kind = &handles[h];
            buffer_queries[h].handle = kind->type;
            image_queries[h].handle = kind->type;
            if (!has_extension(extensions, n, kind->extension)) {
                printf("HANDLE=%s RESULT=not_queried reason=extension_not_advertised extension=%s\n",
                       kind->name, kind->extension);
                continue;
            }
            printf("HANDLE=%s EXTENSION=%s ADVERTISED=1\n", kind->name, kind->extension);
            VkPhysicalDeviceExternalBufferInfo bi = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .handleType = kind->type
            };
            VkExternalBufferProperties bp = {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES
            };
            get_buffer(devices[i], &bi, &bp);
            buffer_queries[h].query = QUERY_OK;
            buffer_queries[h].features = bp.externalMemoryProperties.externalMemoryFeatures;
            buffer_queries[h].compatible = bp.externalMemoryProperties.compatibleHandleTypes;
            report("storage_buffer", kind, bp.externalMemoryProperties);

            VkPhysicalDeviceExternalImageFormatInfo ei = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
                .handleType = kind->type
            };
            VkPhysicalDeviceImageFormatInfo2 ii = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, .pNext = &ei,
                .format = VK_FORMAT_R8G8B8A8_UNORM, .type = VK_IMAGE_TYPE_2D,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            };
            VkExternalImageFormatProperties ep = {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES
            };
            VkImageFormatProperties2 ip = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &ep
            };
            result = get_image(devices[i], &ii, &ip);
            if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
                image_queries[h].query = QUERY_UNSUPPORTED;
                printf("RESOURCE=rgba8_image HANDLE=%s RESULT=format_not_supported\n", kind->name);
            } else if (result != VK_SUCCESS) {
                fprintf(stderr, "FAIL device=%u image_handle=%s query=%d\n", i, kind->name, result);
                failed = 1;
            } else {
                image_queries[h].query = QUERY_OK;
                image_queries[h].features = ep.externalMemoryProperties.externalMemoryFeatures;
                image_queries[h].compatible = ep.externalMemoryProperties.compatibleHandleTypes;
                report("rgba8_image", kind, ep.externalMemoryProperties);
                printf("IMAGE_LIMITS HANDLE=%s MAX_EXTENT=%ux%ux%u MAX_MIPS=%u MAX_LAYERS=%u "
                       "SAMPLE_COUNTS=0x%x MAX_RESOURCE_SIZE=%" PRIu64 "\n", kind->name,
                       ip.imageFormatProperties.maxExtent.width, ip.imageFormatProperties.maxExtent.height,
                       ip.imageFormatProperties.maxExtent.depth, ip.imageFormatProperties.maxMipLevels,
                       ip.imageFormatProperties.maxArrayLayers, ip.imageFormatProperties.sampleCounts,
                       (uint64_t)ip.imageFormatProperties.maxResourceSize);
            }
        }
        report_combination("storage_buffer", buffer_queries, 0, 1);
        report_combination("rgba8_image", image_queries, 0, 1);
        report_combination("storage_buffer", buffer_queries, 1, 2);
        report_combination("rgba8_image", image_queries, 1, 2);
        free(extensions);
    }
    if (!queried_devices) {
        fputs("FAIL no Vulkan 1.1 physical device was queried\n", stderr);
        failed = 1;
    }
done:
    free(devices);
    vkDestroyInstance(instance, NULL);
    if (!failed) puts("QUERY_COMPLETE");
    return failed ? 1 : 0;
}
