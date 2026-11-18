// One headless gfxstream renderer owned by the EMU VM process.
#include "renderer_start.h"
#include "renderer_guest_probe.h"
#include "primary_display.h"
#include "vm_snapshot.h"
#include "FrameBuffer.h"
#include "gfxstream/host/Features.h"
#include "host-common/globals.h"
#include "host-common/opengl/emugl_config.h"
#include "host-common/opengl/gpuinfo.h"
#include "vulkan/VkCommonOperations.h"

#include <android/log.h>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
int positiveInt(const char* value) {
    int result = 0;
    const auto* end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end && result > 0 ? result : 0;
}

int failed(const char* stage, int detail = 0) {
    std::fprintf(stderr, "RME_VK_HOST_RENDERER_%s_FAILED detail=%d\n", stage, detail);
    __android_log_print(ANDROID_LOG_ERROR, "RedMagicEMU-Vulkan", "%s failed (%d)", stage, detail);
    return 1;
}

bool reportAndWait(int readyFd) {
    constexpr char ready[] = "RME_VK_HOST_READY\n";
    ssize_t bytes;
    do { bytes = write(readyFd, ready, sizeof(ready) - 1); } while (bytes < 0 && errno == EINTR);
    close(readyFd);
    if (bytes != sizeof(ready) - 1) return false;
    char command;
    do { bytes = read(STDIN_FILENO, &command, 1); } while (bytes < 0 && errno == EINTR);
    return bytes == 0 || (bytes == 1 && command == 'Q');
}
}

extern "C" int rme_vulkan_renderer_main(int argc, char** argv) {
    const bool probe = (argc == 11 || argc == 12) && std::strcmp(argv[6], "--guest-probe") == 0;
    const bool attach = argc == 9 && std::strcmp(argv[6], "--guest-app") == 0;
    const bool guest = argc == 9 && std::strcmp(argv[6], "--guest-root") == 0;
    if ((!probe && !attach && !guest && argc != 6) || std::strcmp(argv[1], "--parent-pid"))
        return failed("ARGUMENT");
    const int parent = positiveInt(argv[2]);
    const int width = positiveInt(argv[3]), height = positiveInt(argv[4]), density = positiveInt(argv[5]);
    const bool waitForTarget = attach && std::strcmp(argv[8], "wait") == 0;
    const int target = attach && !waitForTarget ? positiveInt(argv[8]) : -1;
    if (parent <= 1 || !width || !height || !density) return failed("ARGUMENT");
    if (attach && !waitForTarget && target <= 1) return failed("ARGUMENT");
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || parent != getppid()) return failed("PARENT");
    // Upstream may write logs to stdout. Only the readiness marker goes on the control pipe.
    const int readyFd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (readyFd < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0 ||
        std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) return failed("CONTROL");

    EmuglConfig config{};
    if (!emuglConfig_init(&config, true, "host", "host", 64, false,
                          isHostGpuBlacklisted() || async_query_host_gpu_VulkanBlacklisted(),
                          false, WINSYS_GLESBACKEND_PREFERENCE_AUTO, true)) return failed("CONFIG");
    emuglConfig_setupEnv(&config);

    // One VM occupies this process for its whole lifetime.
    constexpr uint64_t generation = 1;
    const auto snapshot = rme::bindSnapshotVm(generation, rme::SnapshotCapability::Unavailable);
    const auto display = rme::bindPrimaryDisplay(generation, width, height, density,
        rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    if (!snapshot || !display || !rme::enablePrimaryColorBuffers(display, generation))
        return failed("AGENTS");

    gfxstream::host::FeatureSet features;
    features.Vulkan.enabled = true;
    features.VulkanShaderFloat16Int8.enabled = true;
    const auto started = rme::startRenderer(rme::RendererStart{
        width, height, true, 36, rme::snapshotVmAgent(), rme::primaryWindowAgent(),
        rme::primaryDisplayAgent(), &features, display, generation, true});
    if (started != rme::RendererStartResult::Started)
        return failed("START", static_cast<int>(started));

    auto* fb = gfxstream::FrameBuffer::getFB();
    const auto* vulkan = gfxstream::vk::getGlobalVkEmulation();
    const bool live = fb && fb->isVulkanEnabled() && vulkan && vulkan->live;
    if (live) {
        const auto& props = vulkan->deviceInfo.physdevProps;
        __android_log_print(ANDROID_LOG_INFO, "RedMagicEMU-Vulkan", "renderer ready on %s (Vulkan %u.%u.%u)",
                            props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
                            VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    } else {
        close(readyFd);
        failed("DEVICE");
    }
    int exitCode = 1;
    if (live && probe) {
        close(readyFd);
        exitCode = rme::runGuestDeviceProbe(argv[7], argv[8], argv[9], argv[10], argv[11]);
    } else if (live && attach) {
        exitCode = rme::runGuestApp(argv[7], target, readyFd);
    } else if (live && guest) {
        exitCode = rme::runGuest(argv[7], argv[8], parent, readyFd);
    } else if (live) {
        exitCode = reportAndWait(readyFd) ? 0 : failed("CONTROL");
    }
    const auto stopped = rme::stopRenderer(display, generation);
    if (stopped != rme::RendererStopResult::Stopped || !rme::clearSnapshotVm(snapshot, generation))
        return failed("STOP", static_cast<int>(stopped));
    return exitCode;
}
