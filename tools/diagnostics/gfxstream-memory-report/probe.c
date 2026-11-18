// SPDX-License-Identifier: MIT
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "FAIL %s = %d\n", #call, r_); return 10; }} while (0)

int main(int argc, char** argv) {
    if (argc != 2 || (strcmp(argv[1], "baseline") && strcmp(argv[1], "patched"))) return 2;
    int expect = !strcmp(argv[1], "baseline");
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance; CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "FAIL no physical device\n"); return 11; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return 12;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice gpu = devices[0]; free(devices);
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
    printf("DEVICE=%s API=%u.%u.%u\n", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
           VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    uint32_t n = 0; CHECK(vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, NULL));
    VkExtensionProperties *exts = calloc(n ? n : 1, sizeof(*exts));
    if (!exts) return 12;
    CHECK(vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, exts));
    int found = 0;
    for (uint32_t i = 0; i < n; ++i) {
        printf("EXT=%s:%u\n", exts[i].extensionName, exts[i].specVersion);
        if (!strcmp(exts[i].extensionName, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME)) found = 1;
    }
    free(exts);
    VkPhysicalDeviceDeviceMemoryReportFeaturesEXT report = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT
    };
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &report };
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    printf("MEMORY_REPORT ext=%d feature=%u\n", found, report.deviceMemoryReport);
    printf("CORE_FEATURES=");
    for (size_t i = 0; i < sizeof(features.features); i += sizeof(uint32_t)) {
        uint32_t value = 0; memcpy(&value, (const char*)&features.features + i, sizeof(value));
        printf("%08x", value);
    }
    putchar('\n');
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, NULL);
    VkQueueFamilyProperties *queues = calloc(qn ? qn : 1, sizeof(*queues));
    if (!queues) return 12;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, queues);
    for (uint32_t i = 0; i < qn; ++i)
        printf("QUEUE=%u flags=%u count=%u timestamps=%u granularity=%u,%u,%u\n", i,
               queues[i].queueFlags, queues[i].queueCount, queues[i].timestampValidBits,
               queues[i].minImageTransferGranularity.width, queues[i].minImageTransferGranularity.height,
               queues[i].minImageTransferGranularity.depth);
    uint32_t qi = qn;
    for (uint32_t i=0; i<qn; ++i) if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qi=i; break; }
    free(queues);
    if (qi == qn) { fprintf(stderr, "FAIL no compute queue\n"); return 13; }
    const char *requested = VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME;
    float priority = 1;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
       .queueFamilyIndex = qi, .queueCount=1, .pQueuePriorities=&priority };
    VkPhysicalDeviceDeviceMemoryReportFeaturesEXT enable = {
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT, .deviceMemoryReport=VK_TRUE };
    VkDeviceCreateInfo dci = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext=&enable,
      .queueCreateInfoCount=1, .pQueueCreateInfos=&qci,
      .enabledExtensionCount=1, .ppEnabledExtensionNames=&requested };
    VkDevice device = VK_NULL_HANDLE; VkResult result = vkCreateDevice(gpu, &dci, NULL, &device);
    printf("CREATE_DEVICE_REPORT=%d\n", result);
    if (result == VK_SUCCESS) vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    int ok = found == expect && report.deviceMemoryReport == (VkBool32)expect &&
             result == (expect ? VK_SUCCESS : VK_ERROR_EXTENSION_NOT_PRESENT);
    printf("PROBE_%s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
