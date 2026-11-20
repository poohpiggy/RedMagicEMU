#pragma once

#include <stdio.h>
#include <vulkan/vulkan.h>

static int testMappedBuffer(PFN_vkGetInstanceProcAddr get, VkInstance instance,
                            VkPhysicalDevice physical) {
    enum {
        words = 64, bytes = words * sizeof(uint32_t), mappedCount = 2,
#if defined(RME_TEST_CACHED_MEMORY) || defined(RME_TEST_COHERENT_DEVICE_LOCAL)
        bufferCount = 3,
#else
        bufferCount = mappedCount,
#endif
#ifdef RME_TEST_COHERENT_DEVICE_LOCAL
        explicitCacheSync = 0,
#else
        explicitCacheSync = 1,
#endif
    };
#define INSTANCE(name)                                                          \
    PFN_vk##name name = (PFN_vk##name)get(instance, "vk" #name);                 \
    if (!name) { fprintf(stderr, "Missing vk%s\n", #name); return 1; }
    INSTANCE(GetPhysicalDeviceMemoryProperties);
    INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(CreateDevice);
    INSTANCE(GetDeviceProcAddr);
#undef INSTANCE

#define CHECK(expr) do { \
    VkResult result = (expr); \
    if (result != VK_SUCCESS) { \
        fprintf(stderr, "GUEST_VULKAN_PUBLIC_MEMORY_FAILED %s result=%d\n", #expr, result); \
        return 1; \
    } \
} while (0)

    uint32_t count = 0;
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (!count || count > 64) return 1;
    VkQueueFamilyProperties queues[64];
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    uint32_t family = 0;
    while (family < count && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    if (family == count) return 1;

    const float priority = 1;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
        .queueCount = 1, .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
    };
    VkDevice device;
    CHECK(CreateDevice(physical, &deviceInfo, NULL, &device));
#define DEVICE(name)                                                           \
    PFN_vk##name name = (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name);     \
    if (!name) { fprintf(stderr, "Missing vk%s\n", #name); return 1; }
    DEVICE(GetDeviceQueue);
    DEVICE(CreateBuffer);
    DEVICE(GetBufferMemoryRequirements);
#ifdef RME_TEST_COHERENT_DEVICE_LOCAL
    DEVICE(GetBufferMemoryRequirements2);
#endif
    DEVICE(AllocateMemory);
    DEVICE(BindBufferMemory);
    DEVICE(MapMemory);
    DEVICE(UnmapMemory);
    DEVICE(FlushMappedMemoryRanges);
    DEVICE(InvalidateMappedMemoryRanges);
    DEVICE(CreateCommandPool);
    DEVICE(AllocateCommandBuffers);
    DEVICE(BeginCommandBuffer);
    DEVICE(CmdCopyBuffer);
    DEVICE(CmdFillBuffer);
    DEVICE(CmdPipelineBarrier);
    DEVICE(EndCommandBuffer);
    DEVICE(CreateFence);
    DEVICE(QueueSubmit);
    DEVICE(WaitForFences);
    DEVICE(DestroyFence);
    DEVICE(DestroyCommandPool);
    DEVICE(DestroyBuffer);
    DEVICE(FreeMemory);
    DEVICE(DestroyDevice);
#undef DEVICE

    VkQueue queue;
    GetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties props;
    GetPhysicalDeviceMemoryProperties(physical, &props);
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    VkBuffer buffers[bufferCount];
    VkDeviceMemory memory[bufferCount];
    VkMappedMemoryRange ranges[mappedCount];
    void *mapped[mappedCount];
    for (size_t i = 0; i < bufferCount; ++i) {
        CHECK(CreateBuffer(device, &bufferInfo, NULL, &buffers[i]));
        VkMemoryRequirements req;
        GetBufferMemoryRequirements(device, buffers[i], &req);
#ifdef RME_TEST_COHERENT_DEVICE_LOCAL
        const VkBufferMemoryRequirementsInfo2 query = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, .buffer = buffers[i],
        };
        VkMemoryRequirements2 extended = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        GetBufferMemoryRequirements2(device, &query, &extended);
        const VkMemoryRequirements *also = &extended.memoryRequirements;
        if (also->size != req.size || also->alignment != req.alignment || also->memoryTypeBits != req.memoryTypeBits) {
            fprintf(stderr, "GUEST_VULKAN_PUBLIC_MEMORY_FAILED inconsistent requirements buffer=%zu\n", i);
            return 1;
        }
#endif
        const VkMemoryPropertyFlags mappedFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
#ifdef RME_TEST_CACHED_MEMORY
                                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
#else
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
#endif
        const VkMemoryPropertyFlags flags = i < mappedCount ? mappedFlags :
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint32_t type = 0;
        while (type < props.memoryTypeCount &&
               (!(req.memoryTypeBits & (1u << type)) ||
                (props.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
        if (type == props.memoryTypeCount) {
            fprintf(stderr, "GUEST_VULKAN_PUBLIC_MEMORY_FAILED buffer=%zu flags=0x%x mask=0x%x\n",
                    i, flags, req.memoryTypeBits);
            for (uint32_t j = 0; j < props.memoryTypeCount; ++j)
                fprintf(stderr, "memory type %u flags=0x%x\n", j, props.memoryTypes[j].propertyFlags);
            return 1;
        }
        const VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size, .memoryTypeIndex = type,
        };
        CHECK(AllocateMemory(device, &alloc, NULL, &memory[i]));
        VkResult bind = BindBufferMemory(device, buffers[i], memory[i], 0);
        if (bind != VK_SUCCESS) {
            fprintf(stderr, "GUEST_VULKAN_PUBLIC_MEMORY_FAILED bind buffer=%zu type=%u result=%d\n",
                    i, type, bind);
            return 1;
        }
        if (i >= mappedCount) continue;
        CHECK(MapMemory(device, memory[i], 0, VK_WHOLE_SIZE, 0, &mapped[i]));
        ranges[i] = (VkMappedMemoryRange){
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = memory[i], .size = VK_WHOLE_SIZE,
        };
    }

    uint32_t *input = mapped[0];
    for (uint32_t i = 0; i < words; ++i) input[i] = i * 11 + 5;
    if (explicitCacheSync) CHECK(FlushMappedMemoryRanges(device, 1, ranges));
    const VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family,
    };
    VkCommandPool pool;
    CHECK(CreateCommandPool(device, &poolInfo, NULL, &pool));
    const VkCommandBufferAllocateInfo commandInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = pool, .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    CHECK(AllocateCommandBuffers(device, &commandInfo, &command));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CHECK(BeginCommandBuffer(command, &begin));
    const VkBufferCopy copy = {.size = bytes};
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    };
#if defined(RME_TEST_CACHED_MEMORY) || defined(RME_TEST_COHERENT_DEVICE_LOCAL)
    CmdCopyBuffer(command, buffers[0], buffers[2], 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 1, &barrier, 0, NULL, 0, NULL);
    CmdCopyBuffer(command, buffers[2], buffers[1], 1, &copy);
#else
    CmdCopyBuffer(command, buffers[0], buffers[1], 1, &copy);
#endif
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 1, &barrier, 0, NULL, 0, NULL);
    const uint32_t fill = 0x51a2d9f0;
    CmdFillBuffer(command, buffers[0], 0, bytes, fill);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(EndCommandBuffer(command));
    VkFence fence;
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(CreateFence(device, &fenceInfo, NULL, &fence));
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command,
    };
    CHECK(QueueSubmit(queue, 1, &submit, fence));
    CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000)));
    if (explicitCacheSync) CHECK(InvalidateMappedMemoryRanges(device, mappedCount, ranges));
    const uint32_t *output = mapped[1];
    unsigned errors = 0;
    for (uint32_t i = 0; i < words; ++i) errors += input[i] != fill || output[i] != i * 11 + 5;
    DestroyFence(device, fence, NULL);
    DestroyCommandPool(device, pool, NULL);
    for (size_t i = 0; i < bufferCount; ++i) {
        if (i < mappedCount) UnmapMemory(device, memory[i]);
        DestroyBuffer(device, buffers[i], NULL);
        FreeMemory(device, memory[i], NULL);
    }
    DestroyDevice(device, NULL);
    if (errors) {
        fprintf(stderr, "GUEST_VULKAN_PUBLIC_MEMORY_FAILED mismatches=%u\n", errors);
        return 1;
    }
    puts("GUEST_VULKAN_PUBLIC_MEMORY_OK values=64");
    return 0;
#undef CHECK
}
