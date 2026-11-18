/* Fake Vulkan and fake components: these tests do not execute a GPU shader. */
#include "../host_fixed_compute.h"
#include "../gpu_buffer.h"
#include "../gpu_compute.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

enum scenario {
    GOOD, BUFFER_FAIL, MAP_FAIL, MAP_NULL, FLUSH_FAIL, START_FAIL,
    START_UNKNOWN, WAIT_FAIL, WAIT_TIMEOUT, INVALIDATE_FAIL,
    WRONG_RESULT, DESTROY_FAIL
};
static enum scenario mode;
static int coherent, bad_args, pending, started, disposed;
static char events[64];
static unsigned event_count, device_destroys;
static uint32_t mapped_words[RME_VK_FIXED_WORDS];
static const VkDevice device = (VkDevice)(uintptr_t)1;
static const VkQueue queue = (VkQueue)(uintptr_t)2;
static const VkDeviceMemory memory = (VkDeviceMemory)(uintptr_t)3;

static void record(char event) {
    if (event_count + 1 >= sizeof(events)) { bad_args = 1; return; }
    events[event_count++] = event;
    events[event_count] = 0;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    (void)instance; (void)name; return NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize offset,
                                            VkDeviceSize size, VkMemoryMapFlags flags, void **out) {
    record('M');
    if (d != device || m != memory || offset != 0 || size != VK_WHOLE_SIZE || flags || !out) bad_args = 1;
    if (mode == MAP_FAIL) return VK_ERROR_MEMORY_MAP_FAILED;
    *out = mode == MAP_NULL ? NULL : mapped_words;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice d, VkDeviceMemory m) {
    record('U'); if (d != device || m != memory) bad_args = 1;
}

static int valid_range(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    return d == device && n == 1 && r && r->sType == VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE &&
        r->memory == memory && r->offset == 0 && r->size == VK_WHOLE_SIZE;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    record('F'); if (!valid_range(d,n,r)) bad_args = 1;
    return mode == FLUSH_FAIL ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    record('I'); if (!valid_range(d,n,r)) bad_args = 1;
    return mode == INVALIDATE_FAIL ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice d, const VkAllocationCallbacks *a) {
    (void)d; (void)a; ++device_destroys;
}

enum rme_vk_buffer_status rme_vk_gpu_buffer_create(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    VkDeviceSize size, VkDeviceSize budget, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
    struct rme_vk_gpu_buffer *buffer) {
    record('B');
    if (!instance || get_proc != vkGetInstanceProcAddr || !gpu || gpu->device != device ||
        size != 256 || budget < size || usage != VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ||
        required != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT || !buffer) bad_args = 1;
    if (mode == BUFFER_FAIL) return RME_VK_BUFFER_CREATE_FAILED;
    buffer->device = device; buffer->owner = gpu; buffer->memory = memory; buffer->size = size;
    buffer->memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        (coherent ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0);
    return RME_VK_BUFFER_OK;
}

enum rme_vk_buffer_status rme_vk_gpu_buffer_destroy(struct rme_vk_gpu_buffer *buffer) {
    record('X'); if (!buffer || buffer->memory != memory || pending) bad_args = 1;
    return mode == DESTROY_FAIL ? RME_VK_BUFFER_INVALID_STATE : RME_VK_BUFFER_OK;
}

enum rme_vk_compute_status rme_vk_compute_start(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_device *gpu,
    struct rme_vk_gpu_buffer *buffer, const uint32_t *shader, size_t words,
    const char *entry, uint32_t x, uint32_t y, uint32_t z,
    struct rme_vk_compute_dispatch *state) {
    record('S');
    if (!instance || get_proc != vkGetInstanceProcAddr || !gpu || !buffer || !state ||
        !shader || words < 5 || shader[0] != 0x07230203 || !entry || strcmp(entry, "main") ||
        x != 4 || y != 1 || z != 1) bad_args = 1;
    started = 1;
    if (mode == START_FAIL) return RME_VK_COMPUTE_VULKAN_ERROR;
    pending = 1;
    if (mode == START_UNKNOWN) return RME_VK_COMPUTE_SUBMIT_ERROR;
    return RME_VK_COMPUTE_OK;
}

enum rme_vk_compute_status rme_vk_compute_wait(struct rme_vk_compute_dispatch *state, uint64_t timeout_ns) {
    record('W'); if (!state || !timeout_ns || !pending) bad_args = 1;
    if (mode == WAIT_TIMEOUT) return RME_VK_COMPUTE_TIMEOUT;
    if (mode == WAIT_FAIL) { pending = 0; return RME_VK_COMPUTE_WAIT_ERROR; }
    pending = 0;
    for (unsigned i = 0; i < RME_VK_FIXED_WORDS; ++i) mapped_words[i] = mapped_words[i] * 3u + 7u;
    if (mode == WRONG_RESULT) mapped_words[19] ^= 1u;
    return RME_VK_COMPUTE_OK;
}

enum rme_vk_compute_status rme_vk_compute_dispose(struct rme_vk_compute_dispatch *state) {
    if (!state) bad_args = 1;
    if (!started || disposed) return RME_VK_COMPUTE_OK;
    record('D');
    if (!pending) disposed = 1;
    return pending ? RME_VK_COMPUTE_STILL_PENDING : RME_VK_COMPUTE_OK;
}

static int run_case(enum scenario selected, int is_coherent, enum rme_vk_fixed_status want,
                    const char *expected_events, int expect_output) {
    struct rme_vk_gpu_device gpu = {0};
    uint32_t input[RME_VK_FIXED_WORDS], output[RME_VK_FIXED_WORDS];
    mode = selected; coherent = is_coherent; bad_args = pending = started = disposed = 0;
    event_count = device_destroys = 0; events[0] = 0;
    memset(mapped_words, 0, sizeof(mapped_words));
    gpu.device = device; gpu.queue = queue;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[0] = 16;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[1] = 1;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[2] = 1;
    gpu.selected.properties.limits.maxComputeWorkGroupInvocations = 16;
    for (unsigned i = 0; i < RME_VK_FIXED_WORDS; ++i) { input[i] = 0xffffffe0u + i; output[i] = 0xdeadbeefu; }
    enum rme_vk_fixed_status got = rme_vk_host_fixed_compute((VkInstance)(uintptr_t)4, &gpu, input, output);
    if (got != want || bad_args || device_destroys || strcmp(events, expected_events)) {
        fprintf(stderr, "FAIL case=%d coherent=%d got=%d want=%d events=%s expected=%s bad=%d device_destroy=%u\n",
                selected,is_coherent,got,want,events,expected_events,bad_args,device_destroys); return 1;
    }
    for (unsigned i = 0; i < RME_VK_FIXED_WORDS; ++i) {
        uint32_t expected = expect_output ? input[i] * 3u + 7u : 0xdeadbeefu;
        if (output[i] != expected) { fprintf(stderr,"FAIL output case=%d word=%u\n",selected,i); return 1; }
    }
    return 0;
}

static int invalid_and_limits(void) {
    struct rme_vk_gpu_device gpu = {0};
    uint32_t input[RME_VK_FIXED_WORDS] = {0}, output[RME_VK_FIXED_WORDS] = {0};
    VkInstance instance = (VkInstance)(uintptr_t)4;
    gpu.device = device; gpu.queue = queue;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[0] = 16;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[1] = 1;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[2] = 1;
    gpu.selected.properties.limits.maxComputeWorkGroupInvocations = 16;
    event_count = device_destroys = 0; events[0] = 0;
    if (rme_vk_host_fixed_compute(VK_NULL_HANDLE, &gpu, input, output) != RME_VK_FIXED_FAILED ||
        rme_vk_host_fixed_compute(instance, NULL, input, output) != RME_VK_FIXED_FAILED ||
        rme_vk_host_fixed_compute(instance, &gpu, NULL, output) != RME_VK_FIXED_FAILED ||
        rme_vk_host_fixed_compute(instance, &gpu, input, NULL) != RME_VK_FIXED_FAILED) return 1;
    gpu.queue = VK_NULL_HANDLE;
    if (rme_vk_host_fixed_compute(instance, &gpu, input, output) != RME_VK_FIXED_FAILED) return 1;
    gpu.queue = queue;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[0] = 15;
    if (rme_vk_host_fixed_compute(instance, &gpu, input, output) != RME_VK_FIXED_FAILED) return 1;
    gpu.selected.properties.limits.maxComputeWorkGroupSize[0] = 16;
    gpu.selected.properties.limits.maxComputeWorkGroupInvocations = 15;
    if (rme_vk_host_fixed_compute(instance, &gpu, input, output) != RME_VK_FIXED_FAILED || event_count || device_destroys)
        return 1;
    return 0;
}

int main(void) {
    int fail = invalid_and_limits();
    if (fail) fprintf(stderr,"FAIL invalid argument or device limits\n");
    fail |= run_case(GOOD, 1, RME_VK_FIXED_OK, "BMSWDUX", 1);
    fail |= run_case(GOOD, 0, RME_VK_FIXED_OK, "BMFSWDIUX", 1);
    fail |= run_case(BUFFER_FAIL, 1, RME_VK_FIXED_FAILED, "B", 0);
    fail |= run_case(MAP_FAIL, 1, RME_VK_FIXED_FAILED, "BMX", 0);
    fail |= run_case(MAP_NULL, 1, RME_VK_FIXED_FAILED, "BMUX", 0);
    fail |= run_case(FLUSH_FAIL, 0, RME_VK_FIXED_FAILED, "BMFUX", 0);
    fail |= run_case(START_FAIL, 1, RME_VK_FIXED_FAILED, "BMSDUX", 0);
    fail |= run_case(START_UNKNOWN, 1, RME_VK_FIXED_UNRESOLVED, "BMSD", 0);
    fail |= run_case(WAIT_FAIL, 1, RME_VK_FIXED_FAILED, "BMSWDUX", 0);
    fail |= run_case(WAIT_TIMEOUT, 1, RME_VK_FIXED_UNRESOLVED, "BMSWD", 0);
    fail |= run_case(INVALIDATE_FAIL, 0, RME_VK_FIXED_FAILED, "BMFSWDIUX", 0);
    fail |= run_case(WRONG_RESULT, 1, RME_VK_FIXED_FAILED, "BMSWDUX", 0);
    fail |= run_case(DESTROY_FAIL, 1, RME_VK_FIXED_UNRESOLVED, "BMSWDUX", 0);
    if (fail) return 1;
    puts("HOST_FIXED_COMPUTE_FAKE_TEST_OK (fake Vulkan; no GPU or Guest)"); return 0;
}
