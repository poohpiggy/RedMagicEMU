#include "DescriptorSetVirtualization.h"
#include "Resources.h"

#include <cassert>
#include <cstdio>

using namespace gfxstream::vk;

static void exercise(bool batched) {
    DescriptorPoolAllocationInfo poolInfo{};
    poolInfo.maxSets = 1;
    poolInfo.freePoolIds = {1};
    poolInfo.descriptorCountInfo = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32, 0},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, 1, 0}};
    goldfish_VkDescriptorPool poolHandle{};
    poolHandle.allocInfo = &poolInfo;
    auto pool = reinterpret_cast<VkDescriptorPool>(&poolHandle);

    VkSampler sampler = (VkSampler)1;
    DescriptorSetLayoutInfo layoutInfo{};
    layoutInfo.bindings = {{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                           {8, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler}};
    goldfish_VkDescriptorSetLayout layoutHandle{};
    layoutHandle.layoutInfo = &layoutInfo;
    auto layout = reinterpret_cast<VkDescriptorSetLayout>(&layoutHandle);

    VkDescriptorSet set{};
    if (batched) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                         pool, 1, &layout};
        assert(validateAndApplyVirtualDescriptorSetAllocation(&info, &set) == VK_SUCCESS);
        VkDescriptorSet extra{};
        assert(validateAndApplyVirtualDescriptorSetAllocation(&info, &extra) == VK_ERROR_OUT_OF_POOL_MEMORY);
    } else {
        set = new_from_host_u64_VkDescriptorSet(1);
        applyDescriptorSetAllocation(pool, layout);
        fillDescriptorSetInfoForPool(pool, layout, set);
    }

    auto* reified = as_goldfish_VkDescriptorSet(set)->reified;
    assert(reified->bindings.size() == 2 && reified->bindingIsImmutableSampler.size() == 9);
    assert(reified->bindingIsImmutableSampler[8] && !reified->bindingIsImmutableSampler[2]);
    size_t slots = 0;
    for (const auto& binding : reified->allWrites) slots += binding.size();
    std::printf("%s descriptor write table: %zu bindings, %zu slots\n",
                batched ? "batched" : "synchronous", reified->allWrites.size(), slots);
    std::fflush(stdout);
    if (batched) {
        assert(reified->allWrites.size() == 9 && reified->allWrites[2].size() == 32 &&
               reified->allWrites[8].size() == 1);
        VkDescriptorBufferInfo bufferInfo{};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 3, 1,
                                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr};
        doEmulatedDescriptorWrite(&write, reified);
        assert(reified->allWrites[2][3].type == DescriptorWriteType::BufferInfo);
    } else {
        assert(reified->allWrites.empty());
    }

    auto cleared = clearDescriptorPool(pool, batched);
    assert(cleared.size() == 1 && cleared.front() == set && poolInfo.usedSets == 0);
    assert(poolInfo.allocedSets.empty() && poolInfo.descriptorCountInfo[0].used == 0);
    delete reified;
    delete_goldfish_VkDescriptorSet(set);
}

int main() {
    exercise(true);
    exercise(false);
}
