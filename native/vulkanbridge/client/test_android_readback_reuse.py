#!/usr/bin/env python3
"""Check the real readback present functions with a CPU-only Vulkan peer."""
import argparse
from pathlib import Path
import subprocess
import tempfile


SOURCE = Path(__file__).with_name("android_readback_wsi.c")


def production_functions():
    source = SOURCE.read_text()
    return "\n".join([
        "enum { MAX_IMAGES = 8 };",
        source[source.index("struct ImageSlot {"):source.index("static pthread_mutex_t chains_lock")],
        source[source.index("static VkResult setup_commands"):source.index("static void copy_bgra_row")],
        source[source.index("int rme_readback_present"):],
    ])


TEST = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

struct vk_queue { uint32_t queue_family_index; };
#define VK_FROM_HANDLE(type, name, handle) struct type *name = (struct type *)(handle)
#define HANDLE(type, number) ((type)(uintptr_t)(number))
struct PresentTest;
struct Chain;
static struct Chain *lookup_chain(VkSwapchainKHR handle);
static VkResult post_pixels(struct Chain *chain);
/* production */

enum Failure { NONE, ALLOCATE, BEGIN, END, SUBMIT, WAIT, INVALIDATE, POST };
struct VkCommandBuffer_T {
    VkCommandPool pool;
    VkImage image;
    bool recorded;
    bool reusable;
    bool submitted;
};
static struct PresentTest {
    enum Failure fail;
    struct Chain chain;
    struct VkCommandBuffer_T buffers[20];
    VkCommandBuffer pending;
    VkImage image;
    VkSemaphore semaphore;
    unsigned pools, destroyed, allocated, begins, ends, frees;
    unsigned resets, submits, waits, invalidates, posts;
} test;

static struct Chain *lookup_chain(VkSwapchainKHR handle) {
    return handle == HANDLE(VkSwapchainKHR, 1) ? &test.chain : NULL;
}

VkResult vkCreateFence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    assert(device == test.chain.device && info->flags == 0 && !allocator);
    *out = HANDLE(VkFence, 1);
    return VK_SUCCESS;
}

VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *out) {
    assert(device == test.chain.device && !allocator);
    assert(info->queueFamilyIndex == 3 || info->queueFamilyIndex == 7);
    *out = HANDLE(VkCommandPool, ++test.pools);
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) {
    assert(device == test.chain.device && pool == test.chain.pool && !allocator && !test.pending);
    for (unsigned i = 0; i < test.allocated; ++i)
        if (test.buffers[i].pool == pool) test.buffers[i].pool = VK_NULL_HANDLE;
    ++test.destroyed;
}

VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out) {
    assert(device == test.chain.device && info->commandPool == test.chain.pool);
    assert(info->commandBufferCount == 1 && info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    if (test.fail == ALLOCATE) return VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(test.allocated < sizeof(test.buffers) / sizeof(test.buffers[0]));
    *out = &test.buffers[test.allocated++];
    (*out)->pool = info->commandPool;
    return VK_SUCCESS;
}

void vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer *commands) {
    assert(device == test.chain.device && count == 1 && (*commands)->pool == pool);
    assert(*commands != test.pending);
    (*commands)->pool = VK_NULL_HANDLE;
    ++test.frees;
}

VkResult vkResetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags flags) {
    assert(cmd->pool == test.chain.pool && cmd != test.pending && flags == 0);
    cmd->recorded = false;
    cmd->submitted = false;
    return VK_SUCCESS;
}

VkResult vkBeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo *info) {
    assert(cmd->pool == test.chain.pool && cmd != test.pending && !cmd->recorded);
    ++test.begins;
    cmd->reusable = !(info->flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return test.fail == BEGIN ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}

VkResult vkEndCommandBuffer(VkCommandBuffer cmd) {
    assert(cmd->image == test.image && !cmd->recorded);
    ++test.ends;
    if (test.fail == END) return VK_ERROR_OUT_OF_HOST_MEMORY;
    cmd->recorded = true;
    return VK_SUCCESS;
}

void vkCmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffer,
    uint32_t image_count, const VkImageMemoryBarrier *image) {
    (void)src;
    (void)dst;
    assert(cmd->pool == test.chain.pool && !flags && !buffer_count && !buffer && image_count == 1);
    assert(image->image == test.image && (memory_count == 0 || (memory_count == 1 && memory)));
}

void vkCmdCopyImageToBuffer(VkCommandBuffer cmd, VkImage image, VkImageLayout layout,
    VkBuffer buffer, uint32_t count, const VkBufferImageCopy *regions) {
    assert(image == test.image && layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    assert(buffer == test.chain.staging && count == 1);
    assert(regions->imageExtent.width == 912 && regions->imageExtent.height == 2016);
    cmd->image = image;
}

VkResult vkResetFences(VkDevice device, uint32_t count, const VkFence *fences) {
    assert(device == test.chain.device && count == 1 && *fences == test.chain.complete && !test.pending);
    ++test.resets;
    return VK_SUCCESS;
}

VkResult vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *submit, VkFence fence) {
    assert(queue && count == 1 && !test.pending && fence == test.chain.complete);
    assert(submit->waitSemaphoreCount == 1 && *submit->pWaitSemaphores == test.semaphore);
    assert(*submit->pWaitDstStageMask == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(submit->commandBufferCount == 1);
    VkCommandBuffer cmd = *submit->pCommandBuffers;
    assert(cmd->pool == test.chain.pool && cmd->image == test.image && cmd->recorded);
    assert(!cmd->submitted || cmd->reusable);
    ++test.submits;
    if (test.fail == SUBMIT) return VK_ERROR_DEVICE_LOST;
    cmd->submitted = true;
    test.pending = cmd;
    return VK_SUCCESS;
}

VkResult vkWaitForFences(VkDevice device, uint32_t count, const VkFence *fences,
    VkBool32 all, uint64_t timeout) {
    assert(device == test.chain.device && count == 1 && *fences == test.chain.complete);
    assert(test.pending && all && timeout == UINT64_MAX);
    ++test.waits;
    test.pending = VK_NULL_HANDLE;
    return test.fail == WAIT ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

VkResult vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges) {
    assert(device == test.chain.device && count == 1 && !test.pending);
    assert(ranges->memory == test.chain.staging_memory && ranges->size == VK_WHOLE_SIZE);
    ++test.invalidates;
    return test.fail == INVALIDATE ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static VkResult post_pixels(struct Chain *chain) {
    assert(chain == &test.chain && !test.pending);
    ++test.posts;
    return test.fail == POST ? VK_ERROR_SURFACE_LOST_KHR : VK_SUCCESS;
}

static void initialize(void) {
    test = (struct PresentTest){
        .chain = {.device = HANDLE(VkDevice, 1), .count = 2, .extent = {912, 2016},
            .staging = HANDLE(VkBuffer, 2), .staging_memory = HANDLE(VkDeviceMemory, 3),
            .slots = {{.image = HANDLE(VkImage, 4)}, {.image = HANDLE(VkImage, 5)}}},
    };
}

static VkResult present(uint32_t index, uint32_t family) {
    struct vk_queue queue = {.queue_family_index = family};
    VkSwapchainKHR handle = HANDLE(VkSwapchainKHR, 1);
    VkResult result = VK_NOT_READY, per_chain = VK_NOT_READY;
    test.image = test.chain.slots[index].image;
    if (!test.chain.retired) test.chain.slots[index].acquired = true;
    /* Change the semaphore on every submit to ensure it is never cached. */
    test.semaphore = HANDLE(VkSemaphore, test.submits + 1);
    const VkPresentInfoKHR info = {.swapchainCount = 1, .pSwapchains = &handle,
        .pImageIndices = &index, .pResults = &per_chain,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &test.semaphore};
    assert(rme_readback_present((VkQueue)&queue, &info, &result));
    assert(result == per_chain && !test.chain.slots[index].acquired);
    return result;
}

static void check_reuse(void) {
    initialize();
    for (uint32_t i = 0; i < 4; ++i) assert(present(i % 2, 3) == VK_SUCCESS);
    if (test.begins != 2) {
        fprintf(stderr, "expected 2 recordings for 4 presents, got %u\n", test.begins);
        exit(EXIT_FAILURE);
    }
    assert(test.allocated == 2 && test.ends == 2);
    assert(test.resets == 4 && test.submits == 4 && test.waits == 4);
    assert(test.invalidates == 4 && test.posts == 4);
    for (uint32_t i = 0; i < 4; ++i) assert(present(i % 2, 7) == VK_SUCCESS);
    assert(test.pools == 2 && test.destroyed == 1);
    assert(test.begins == 4 && test.allocated == 4 && test.posts == 8);
    assert(present(0, 3) == VK_SUCCESS);
    assert(test.pools == 3 && test.destroyed == 2 && test.begins == 5);
}

static void check_failures(void) {
    for (enum Failure fail = ALLOCATE; fail <= POST; ++fail) {
        initialize();
        test.fail = fail;
        VkResult expected = fail <= END ? VK_ERROR_OUT_OF_HOST_MEMORY :
            fail == POST ? VK_ERROR_SURFACE_LOST_KHR : VK_ERROR_DEVICE_LOST;
        assert(present(1, 3) == expected);
        assert(test.chain.retired);
        assert(test.frees == (unsigned)(fail == BEGIN || fail == END));
        assert(test.submits == (unsigned)(fail >= SUBMIT));
        assert(test.posts == (unsigned)(fail == POST));
        test.fail = NONE;
        assert(present(1, 3) == VK_ERROR_OUT_OF_DATE_KHR);
        assert(test.submits == (unsigned)(fail >= SUBMIT));
    }
}

int main(void) {
    check_reuse();
    check_failures();
    puts("PASS: readback recording reuse, queue-family changes, and present failure handling");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vulkan_include", type=Path, help="Directory containing vulkan/vulkan_core.h")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="rme-vk-readback-") as work:
        source = Path(work) / "test.c"
        source.write_text(TEST.replace("/* production */", production_functions()), encoding="utf-8")
        executable = Path(work) / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(args.vulkan_include), source, "-o", executable], check=True)
        subprocess.run([executable], check=True)


if __name__ == "__main__":
    main()
