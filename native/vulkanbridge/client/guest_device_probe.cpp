// Uses the paired gfxstream encoder rather than the Guest's vendor driver.
#include "QemuPipeStream.h"
#include "VkEncoder.h"
#include "guest_compute_probe.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
int qemu_pipe_open(const char* name);
void qemu_pipe_close(int fd);
int qemu_pipe_write_fully(int fd, const void* buffer, int size);
int qemu_pipe_read_fully(int fd, void* buffer, int size);
}

namespace {
int fail(const char* operation, int result) {
    std::fprintf(stderr, "GUEST_VULKAN_PROBE_FAILED %s result=%d\n", operation, result);
    return 1;
}

int enumerate(uint64_t puid, bool compute, ComputeReadback mode, bool expectMismatch) {
    QemuPipeStream stream;
    if (stream.connect() < 0) return fail("graphics pipe", -1);
    const uint32_t clientFlags = 0;
    // Standard rcSetPuid packet for the no-checksum, Vulkan-only channel.
    const struct {
        uint32_t opcode;
        uint32_t size;
        uint64_t value;
    } setPuid{10033, 16, puid};
    static_assert(sizeof(setPuid) == 16);
    if (stream.writeFully(&clientFlags, sizeof(clientFlags)) < 0 ||
        stream.writeFully(&setPuid, sizeof(setPuid)) < 0) return fail("channel handshake", -1);

    gfxstream::vk::VkEncoder client(&stream);
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "RedMagicEMU Guest GPU probe";
    application.pEngineName = "RedMagicEMU";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    auto result = client.vkCreateInstance(&create, nullptr, &instance, false);
    if (result != VK_SUCCESS || !instance) return fail("vkCreateInstance", result);

    uint32_t count = 1;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    result = client.vkEnumeratePhysicalDevices(instance, &count, &device, false);
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count != 1 || !device) {
        client.vkDestroyInstance(instance, nullptr, false);
        return fail("vkEnumeratePhysicalDevices", result);
    }
    VkPhysicalDeviceProperties props{};
    client.vkGetPhysicalDeviceProperties(device, &props, false);
    const int computeStatus = compute ? checkGuestCompute(client, stream, device, props, mode, expectMismatch) : 0;
    // A failed wait leaves the device in use. End the probe without destroying
    // its Vulkan objects and let the renderer retire its session.
    if (computeStatus == 2) return 2;
    client.vkDestroyInstance(instance, nullptr, false);
    uint32_t version = 0;
    result = client.vkEnumerateInstanceVersion(&version, false);
    if (result != VK_SUCCESS || !version || !props.apiVersion || !props.vendorID ||
        !props.deviceName[0] || !std::memchr(props.deviceName, '\0', sizeof(props.deviceName)))
        return fail("device properties", result);
    std::printf("GUEST_VULKAN_DEVICE_OK name=%s vendor=0x%04x device=0x%04x api=%u.%u.%u\n",
                props.deviceName, props.vendorID, props.deviceID, VK_API_VERSION_MAJOR(props.apiVersion),
                VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    return computeStatus;
}
}

int main(int argc, char** argv) {
    const char* option = argc == 2 ? argv[1] : "";
    const bool buffer = !std::strcmp(option, "--readback") || !std::strcmp(option, "--readback-expect-mismatch");
    const bool bad = !std::strcmp(option, "--expect-mismatch") || !std::strcmp(option, "--readback-expect-mismatch");
    const bool compute = buffer || bad || !std::strcmp(option, "--compute");
    if (argc != 1 && !compute) return fail("arguments", 0);
    const int process = qemu_pipe_open("GLProcessPipe");
    if (process < 0) return fail("process pipe", process);
    const int32_t confirm = 100;
    uint64_t puid = 0;
    const bool connected = !qemu_pipe_write_fully(process, &confirm, sizeof(confirm)) &&
                           !qemu_pipe_read_fully(process, &puid, sizeof(puid)) && puid;
    const auto mode = buffer ? ComputeReadback::Buffer : ComputeReadback::Query;
    const int status = connected ? enumerate(puid, compute, mode, bad) : fail("process handshake", -1);
    qemu_pipe_close(process);
    return status;
}
