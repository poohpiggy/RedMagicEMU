#include "guest_compute_probe.h"
#include "QemuPipeStream.h"
#include "Resources.h"
#include "VkEncoder.h"
#include "VulkanStreamGuest.h"
#include "goldfish_vk_counting_guest.h"
#include "goldfish_vk_marshaling_guest.h"
#include "goldfish_vk_reserved_marshaling_guest.h"
#include "guest_probe_shaders.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t kWords = 64;
constexpr uint32_t kCalibrationQueries = 2;
constexpr uint32_t kQueryCount = kWords + kCalibrationQueries;
constexpr size_t kBufferBytes = kWords * sizeof(VkDispatchIndirectCommand);
static_assert(sizeof(VkDispatchIndirectCommand) == 3 * sizeof(uint32_t));
using gfxstream::vk::VkEncoder;

int fail(const char* operation, int result) {
    std::fprintf(stderr, "GUEST_VULKAN_COMPUTE_FAILED %s result=%d\n", operation, result);
    return 1;
}

#define CHECK(call) do { const auto result = client.call; \
    if (result != VK_SUCCESS) return fail(#call, result); } while (false)

struct Resources {
    VkEncoder& client;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::array<VkShaderModule, 2> shaders{};
    std::array<VkPipeline, 2> pipelines{};
    VkQueryPool queries = VK_NULL_HANDLE;
    VkCommandPool commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool pending = false;

    ~Resources() {
        if (!device || pending) return;
        if (fence) client.vkDestroyFence(device, fence, nullptr, false);
        if (commands) client.vkDestroyCommandPool(device, commands, nullptr, false);
        if (queries) client.vkDestroyQueryPool(device, queries, nullptr, false);
        for (auto pipeline : pipelines)
            if (pipeline) client.vkDestroyPipeline(device, pipeline, nullptr, false);
        for (auto shader : shaders)
            if (shader) client.vkDestroyShaderModule(device, shader, nullptr, false);
        if (pipelineLayout) client.vkDestroyPipelineLayout(device, pipelineLayout, nullptr, false);
        if (descriptors) client.vkDestroyDescriptorPool(device, descriptors, nullptr, false);
        if (layout) client.vkDestroyDescriptorSetLayout(device, layout, nullptr, false);
        if (buffer) client.vkDestroyBuffer(device, buffer, nullptr, false);
        if (memory) client.vkFreeMemory(device, memory, nullptr, false);
        client.vkDestroyDevice(device, nullptr, false);
    }
};

uint32_t inputWord(uint32_t index) { return (17u * index + 5u) % kWords; }

// Use the existing copy reply from the paired decoder. The diagnostic owns
// the output array, while gfxstream owns the actual host Vulkan mapping.
bool readBuffer(QemuPipeStream& stream, VkDevice device, VkDeviceMemory memory,
                std::array<uint32_t, kWords * 3>& output) {
    using namespace gfxstream::vk;
    const VkMappedMemoryRange range{.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory, .size = kBufferBytes};
    const uint32_t opcode = OP_vkInvalidateMappedMemoryRanges;
    const uint64_t boxedDevice = get_host_u64_VkDevice(device);
    const uint32_t ranges = 1;
    VulkanStreamGuest codec(&stream);
    size_t total = sizeof(opcode) + sizeof(uint32_t) + sizeof(boxedDevice) + sizeof(ranges);
    count_VkMappedMemoryRange(codec.getFeatureBits(), VK_STRUCTURE_TYPE_MAX_ENUM, &range, &total);
    const uint32_t length = total;
    std::vector<uint8_t> message(length);
    auto* next = message.data();
    const auto append = [&next](const auto& value) {
        std::memcpy(next, &value, sizeof(value));
        next += sizeof(value);
    };
    append(opcode);
    append(length);
    append(boxedDevice);
    append(ranges);
    reservedmarshal_VkMappedMemoryRange(&codec, VK_STRUCTURE_TYPE_MAX_ENUM, &range, &next);
    if (next != message.data() + message.size() ||
        stream.writeFully(message.data(), message.size()) < 0) return false;
    VkResult result = VK_ERROR_UNKNOWN;
    uint64_t returned = 0;
    if (!stream.readFully(&result, sizeof(result)) ||
        !stream.readFully(&returned, sizeof(returned))) return false;
    if (result != VK_SUCCESS || returned != kBufferBytes) {
        std::fprintf(stderr, "GUEST_VULKAN_BUFFER_REPLY_FAILED result=%d bytes=%" PRIu64 "\n",
                     result, returned);
        return false;
    }
    return stream.readFully(output.data(), kBufferBytes) != nullptr;
}
}

int checkGuestCompute(VkEncoder& client, QemuPipeStream& stream, VkPhysicalDevice physical,
                      const VkPhysicalDeviceProperties& properties, ComputeReadback readback,
                      bool expectMismatch) {
    const bool queriesEnabled = readback == ComputeReadback::Query;
    VkPhysicalDeviceFeatures supported{};
    client.vkGetPhysicalDeviceFeatures(physical, &supported, false);
    if (queriesEnabled && !supported.pipelineStatisticsQuery) return fail("pipelineStatisticsQuery", 0);

    uint32_t familyCount = 0;
    client.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr, false);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    client.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data(), false);
    uint32_t family = 0;
    while (family < familyCount && (!families[family].queueCount ||
           !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT))) ++family;
    if (family == familyCount) return fail("compute queue family", 0);

    Resources objects{client};
    VkPhysicalDeviceFeatures requested{};
    requested.pipelineStatisticsQuery = queriesEnabled;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const VkDeviceCreateInfo deviceInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueInfo, .pEnabledFeatures = &requested};
    CHECK(vkCreateDevice(physical, &deviceInfo, nullptr, &objects.device, false));
    const auto device = objects.device;
    VkQueue queue = VK_NULL_HANDLE;
    client.vkGetDeviceQueue(device, family, 0, &queue, false);
    if (!queue) return fail("vkGetDeviceQueue", 0);

    // The first 64 records are indirect dispatch arguments. The final 64 words
    // are per-lane scratch counters used by the measured shader.
    std::array<uint32_t, 4 * kWords> input{};
    for (uint32_t i = 0; i < kWords; ++i) {
        input[3 * i] = inputWord(i);
        input[3 * i + 1] = input[3 * i + 2] = 1;
    }
    const VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(input), .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
    CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &objects.buffer, false));
    VkMemoryRequirements requirements{};
    client.vkGetBufferMemoryRequirements(device, objects.buffer, &requirements, false);
    VkPhysicalDeviceMemoryProperties memoryTypes{};
    client.vkGetPhysicalDeviceMemoryProperties(physical, &memoryTypes, false);
    uint32_t memoryType = 0;
    while (memoryType < memoryTypes.memoryTypeCount) {
        const bool visible = memoryTypes.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if ((requirements.memoryTypeBits & (1u << memoryType)) && (queriesEnabled || visible)) break;
        ++memoryType;
    }
    if (memoryType == memoryTypes.memoryTypeCount) return fail("buffer memory type", 0);
    if (!queriesEnabled && (kBufferBytes > requirements.size ||
        (kBufferBytes != requirements.size && kBufferBytes % properties.limits.nonCoherentAtomSize)))
        return fail("buffer readback range alignment", 0);
    const VkMemoryAllocateInfo allocation{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memoryType};
    CHECK(vkAllocateMemory(device, &allocation, nullptr, &objects.memory, false));
    CHECK(vkBindBufferMemory(device, objects.buffer, objects.memory, 0, false));

    const VkDescriptorSetLayoutBinding binding{.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &objects.layout, false));
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    const VkDescriptorPoolCreateInfo descriptors{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &poolSize};
    CHECK(vkCreateDescriptorPool(device, &descriptors, nullptr, &objects.descriptors, false));
    const VkDescriptorSetAllocateInfo descriptorInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = objects.descriptors, .descriptorSetCount = 1, .pSetLayouts = &objects.layout};
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    CHECK(vkAllocateDescriptorSets(device, &descriptorInfo, &descriptor, false));
    const VkDescriptorBufferInfo descriptorBuffer{objects.buffer, 0, sizeof(input)};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &descriptorBuffer};
    client.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr, false);

    const VkPipelineLayoutCreateInfo pipelineLayout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &objects.layout};
    CHECK(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &objects.pipelineLayout, false));
    const VkShaderModuleCreateInfo shaders[] = {
        {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = sizeof(kTransformSpv), .pCode = kTransformSpv},
        {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = sizeof(kCountSpv), .pCode = kCountSpv},
    };
    for (size_t i = 0; i < (queriesEnabled ? objects.pipelines.size() : 1); ++i) {
        CHECK(vkCreateShaderModule(device, &shaders[i], nullptr, &objects.shaders[i], false));
        const VkComputePipelineCreateInfo pipeline{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = objects.shaders[i], .pName = "main"},
            .layout = objects.pipelineLayout};
        CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &objects.pipelines[i], false));
    }
    const VkQueryPoolCreateInfo queries{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS, .queryCount = kQueryCount,
        .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT};
    if (queriesEnabled) CHECK(vkCreateQueryPool(device, &queries, nullptr, &objects.queries, false));
    const VkCommandPoolCreateInfo commands{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(device, &commands, nullptr, &objects.commands, false));
    const VkCommandBufferAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = objects.commands, .commandBufferCount = 1};
    VkCommandBuffer command = VK_NULL_HANDLE;
    CHECK(vkAllocateCommandBuffers(device, &allocate, &command, false));
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(command, &begin, false));

    if (queriesEnabled) client.vkCmdResetQueryPool(command, objects.queries, 0, kQueryCount, false);
    client.vkCmdUpdateBuffer(command, objects.buffer, 0, sizeof(input), input.data(), false);
    const VkMemoryBarrier uploaded{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    client.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, 1, &uploaded, 0, nullptr, 0, nullptr, false);
    client.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, objects.pipelineLayout,
                                  0, 1, &descriptor, 0, nullptr, false);
    client.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, objects.pipelines[0], false);
    client.vkCmdDispatch(command, kWords / 16, 1, 1, false);
    const VkMemoryBarrier transformed{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | (queriesEnabled ? 0u : VK_ACCESS_TRANSFER_WRITE_BIT),
        .dstAccessMask = queriesEnabled ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : VK_ACCESS_HOST_READ_BIT};
    const auto writtenBy = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (queriesEnabled ? 0u : VK_PIPELINE_STAGE_TRANSFER_BIT);
    const auto nextStage = queriesEnabled ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : VK_PIPELINE_STAGE_HOST_BIT;
    client.vkCmdPipelineBarrier(command, writtenBy, nextStage,
                               0, 1, &transformed, 0, nullptr, 0, nullptr, false);
    if (queriesEnabled) {
        client.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, objects.pipelines[1], false);
        for (uint32_t i = 0; i < kQueryCount; ++i) {
            client.vkCmdBeginQuery(command, objects.queries, i, 0, false);
            if (i < kCalibrationQueries) {
                client.vkCmdDispatch(command, i + 1, 1, 1, false);
            } else {
                const auto offset = (i - kCalibrationQueries) * sizeof(VkDispatchIndirectCommand);
                client.vkCmdDispatchIndirect(command, objects.buffer, offset, false);
            }
            client.vkCmdEndQuery(command, objects.queries, i, false);
        }
    }
    CHECK(vkEndCommandBuffer(command, false));
    const VkFenceCreateInfo fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(device, &fence, nullptr, &objects.fence, false));
    const VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command};
    objects.pending = true;
    auto result = client.vkQueueSubmit(queue, 1, &submit, objects.fence, false);
    if (result != VK_SUCCESS) { fail("vkQueueSubmit", result); return 2; }
    result = client.vkWaitForFences(device, 1, &objects.fence, VK_TRUE, 4'000'000'000ULL, false);
    if (result != VK_SUCCESS) { fail("vkWaitForFences", result); return 2; }
    objects.pending = false;

    std::array<uint64_t, kQueryCount> observations{};
    uint64_t scale = 1;
    if (queriesEnabled) {
        CHECK(vkGetQueryPoolResults(device, objects.queries, 0, kQueryCount, sizeof(observations),
                                   observations.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT, false));
        scale = observations[0];
        if (!scale || observations[1] != 2 * scale) {
            std::fprintf(stderr, "GUEST_VULKAN_COMPUTE_CALIBRATION_FAILED one=%" PRIu64 " two=%" PRIu64 "\n",
                         observations[0], observations[1]);
            return 1;
        }
    } else {
        std::array<uint32_t, kWords * 3> buffer{};
        if (!readBuffer(stream, device, objects.memory, buffer)) {
            objects.pending = true;  // The wire position after a failed copy reply is unknown.
            fail("buffer readback", 0);
            return 2;
        }
        for (uint32_t i = 0; i < kWords; ++i) {
            if (buffer[i * 3 + 1] != 1 || buffer[i * 3 + 2] != 1)
                return fail("untouched buffer words", i);
            observations[i + kCalibrationQueries] = buffer[i * 3];
        }
    }
    unsigned mismatches = 0;
    for (uint32_t i = 0; i < kWords; ++i) {
        const auto expected = scale * (3 * inputWord(i) + 7 + (expectMismatch && i == 0));
        const auto got = observations[i + kCalibrationQueries];
        if (got != expected) {
            if (!mismatches)
                std::fprintf(stderr, "GUEST_VULKAN_COMPUTE_MISMATCH index=%u got=%" PRIu64
                             " expected=%" PRIu64 "\n", i, got, expected);
            ++mismatches;
        }
    }
    if (mismatches) return fail(queriesEnabled ? "query result mismatches" : "buffer result mismatches", mismatches);
    if (queriesEnabled) {
        std::printf("GUEST_VULKAN_COMPUTE_OK values=%u invocations_per_group=%" PRIu64 "\n", kWords, scale);
    } else {
        std::printf("GUEST_VULKAN_BUFFER_READBACK_OK values=%u bytes=%zu\n", kWords, kBufferBytes);
    }
    return 0;
}
