/* Standalone headless Vulkan compute probe; no EMU or Android services. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_spv.h"
#include "verify.h"
#include "device_rank.h"

enum { WORD_COUNT = 64 };
#define CHECK(call) do { VkResult check_result_ = (call); if (check_result_ != VK_SUCCESS) { \
    fprintf(stderr, "FAIL %s: VkResult %d\n", #call, (int)check_result_); goto cleanup; } } while (0)

static const char *device_type(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU software";
    default: return "other/unknown";
    }
}

int main(int argc, char **argv) {
    int bad_expectation = 0, result = 1, mapped = 0, queue_may_be_busy = 0;
    if (argc == 2 && !strcmp(argv[1], "--expect-mismatch")) bad_expectation = 1;
    else if (argc != 1) { fprintf(stderr, "usage: %s [--expect-mismatch]\n", argv[0]); return 2; }
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkPhysicalDevice *physical = NULL;
    VkQueueFamilyProperties *families = NULL;
    uint32_t *words = NULL;
    VkPhysicalDevice selected = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties selected_properties = {0};
    uint32_t family = 0, count = 0;
    int selected_rank = 0;
    const VkDeviceSize bytes = WORD_COUNT * sizeof(uint32_t);

    uint32_t loader_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerate_version) CHECK(enumerate_version(&loader_version));
    printf("LOADER api=%u.%u.%u\n", VK_API_VERSION_MAJOR(loader_version),
        VK_API_VERSION_MINOR(loader_version), VK_API_VERSION_PATCH(loader_version));
    if (loader_version < VK_API_VERSION_1_2) {
        fprintf(stderr, "FAIL Vulkan loader version is below 1.2\n"); goto cleanup;
    }
    VkApplicationInfo application = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "rme-vulkan-compute-probe", .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo instance_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application };
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "FAIL no Vulkan physical devices\n"); goto cleanup; }
    physical = calloc(count, sizeof(*physical));
    if (!physical) { fprintf(stderr, "FAIL physical-device allocation\n"); goto cleanup; }
    CHECK(vkEnumeratePhysicalDevices(instance, &count, physical));
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical[i], &properties);
        printf("CANDIDATE name=%s type=%s api=%u.%u.%u\n", properties.deviceName,
            device_type(properties.deviceType), VK_API_VERSION_MAJOR(properties.apiVersion),
            VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
        if (properties.apiVersion < VK_API_VERSION_1_2) continue;
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical[i], &family_count, NULL);
        if (!family_count) continue;
        families = calloc(family_count, sizeof(*families));
        if (!families) { fprintf(stderr, "FAIL queue-family allocation\n"); goto cleanup; }
        vkGetPhysicalDeviceQueueFamilyProperties(physical[i], &family_count, families);
        int rank = device_rank(properties.deviceType);
        for (uint32_t j = 0; j < family_count; ++j) {
            if (rank > selected_rank && families[j].queueCount && (families[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                selected = physical[i]; selected_properties = properties; family = j; selected_rank = rank;
                break;
            }
        }
        free(families); families = NULL;
    }
    if (!selected) { fprintf(stderr, "FAIL no Vulkan 1.2 compute queue\n"); goto cleanup; }
    printf("DEVICE name=%s type=%s api=%u.%u.%u queue=%u vendor=0x%04x device=0x%04x driver=0x%08x\n", selected_properties.deviceName,
        device_type(selected_properties.deviceType), VK_API_VERSION_MAJOR(selected_properties.apiVersion),
        VK_API_VERSION_MINOR(selected_properties.apiVersion), VK_API_VERSION_PATCH(selected_properties.apiVersion), family,
        selected_properties.vendorID, selected_properties.deviceID, selected_properties.driverVersion);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info };
    CHECK(vkCreateDevice(selected, &device_info, NULL, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);
    if (!queue) { fprintf(stderr, "FAIL missing compute queue\n"); goto cleanup; }
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(selected, &properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if (!(requirements.memoryTypeBits & (1u << i)) || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        if (memory_type == UINT32_MAX || (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) memory_type = i;
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
    }
    if (memory_type == UINT32_MAX) { fprintf(stderr, "FAIL no host-visible buffer memory type\n"); goto cleanup; }
    int coherent = !!(properties.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    printf("MEMORY host_visible=yes host_coherent=%s\n", coherent ? "yes" : "no");
    VkMemoryAllocateInfo allocate_info = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type };
    CHECK(vkAllocateMemory(device, &allocate_info, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void *address = NULL;
    CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &address));
    mapped = 1;
    if (!address) { fprintf(stderr, "FAIL map returned a null address\n"); goto cleanup; }
    words = address;
    for (uint32_t i = 0; i < WORD_COUNT; ++i) words[i] = i;
    VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory, .offset = 0, .size = VK_WHOLE_SIZE };
    if (!coherent) CHECK(vkFlushMappedMemoryRanges(device, 1, &range));

    VkDescriptorSetLayoutBinding binding = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo layout_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding };
    CHECK(vkCreateDescriptorSetLayout(device, &layout_info, NULL, &layout));
    VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &pool));
    VkDescriptorSetAllocateInfo descriptor_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout };
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    CHECK(vkAllocateDescriptorSets(device, &descriptor_info, &descriptor));
    VkDescriptorBufferInfo descriptor_buffer = { .buffer = buffer, .offset = 0, .range = bytes };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor,
        .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor_buffer };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkPipelineLayoutCreateInfo pipeline_layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &layout };
    CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));
    VkShaderModuleCreateInfo shader_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(shader_spv), .pCode = shader_spv };
    CHECK(vkCreateShaderModule(device, &shader_info, NULL, &shader));
    VkPipelineShaderStageCreateInfo stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main" };
    VkComputePipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage, .layout = pipeline_layout };
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
    VkCommandPoolCreateInfo command_pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family };
    CHECK(vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool));
    VkCommandBufferAllocateInfo command_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command = VK_NULL_HANDLE;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(command, &begin_info));
    VkMemoryBarrier host_to_shader = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &host_to_shader, 0, NULL, 0, NULL);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor, 0, NULL);
    vkCmdDispatch(command, WORD_COUNT / 16, 1, 1);
    VkMemoryBarrier shader_to_host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &shader_to_host, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command };
    /* A failed submit/wait cannot establish that resources are no longer in use.
     * Leave them to process teardown on that path rather than destroy them. */
    queue_may_be_busy = 1;
    VkResult submit_result = vkQueueSubmit(queue, 1, &submit, fence);
    if (submit_result != VK_SUCCESS) {
        fprintf(stderr, "FAIL vkQueueSubmit: VkResult %d\n", (int)submit_result); goto cleanup;
    }
    VkResult wait_result = vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull);
#ifdef RME_TEST_WAIT_FAILURE
    /* Test only: wait for actual completion, then exercise the timeout path. */
    if (wait_result == VK_SUCCESS) wait_result = VK_TIMEOUT;
#endif
    if (wait_result != VK_SUCCESS) {
        fprintf(stderr, "FAIL vkWaitForFences: VkResult %d\n", (int)wait_result); goto cleanup;
    }
    queue_may_be_busy = 0;
    if (!coherent) CHECK(vkInvalidateMappedMemoryRanges(device, 1, &range));
    size_t errors = verify_words(words, WORD_COUNT, bad_expectation);
    if (errors) { fprintf(stderr, "FAIL output mismatches=%zu%s\n", errors,
                          bad_expectation ? " (intentional expectation mismatch)" : ""); goto cleanup; }
    puts("VULKAN_COMPUTE_OK words=64");
    result = 0;

cleanup:
    free(families); free(physical);
    if (queue_may_be_busy) {
        fprintf(stderr, "QUEUE_COMPLETION_UNKNOWN: leaving Vulkan resources for process teardown\n");
        return 1;
    }
    if (device) {
        if (mapped) vkUnmapMemory(device, memory);
        if (fence) vkDestroyFence(device, fence, NULL);
        if (command_pool) vkDestroyCommandPool(device, command_pool, NULL);
        if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
        if (shader) vkDestroyShaderModule(device, shader, NULL);
        if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (pool) vkDestroyDescriptorPool(device, pool, NULL);
        if (layout) vkDestroyDescriptorSetLayout(device, layout, NULL);
        if (buffer) vkDestroyBuffer(device, buffer, NULL);
        if (memory) vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
    }
    if (instance) vkDestroyInstance(instance, NULL);
    return result;
}
