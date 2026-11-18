#include "renderer_start.h"
#include "primary_display.h"
#include "vm_snapshot.h"
#include "../gfxstream-android-gpuinfo/query.h"
#include "host-common/opengl/emugl_config.h"
#include "host-common/opengl/gpuinfo.h"
#include "host-common/window_agent.h"
#include "host-common/multi_display_agent.h"
#include "host-common/vm_operations.h"
#include "host-common/opengles.h"
#include "gfxstream/host/Features.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

// All platform state below is a test substitute; no GPU or renderer is run.
static GpuInfoList globalList, anotherList;
static int waits = 0;
static bool noLibrary = false;
static int library, instance, device;
static void* openLibrary(const char*, int) { return noLibrary ? nullptr : &library; }
static int closeLibrary(void*) { return 0; }
static VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* out) {
    *out = reinterpret_cast<VkInstance>(&instance); return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance, uint32_t* count, VkPhysicalDevice* devices) {
    *count = 1; if (devices) devices[0] = reinterpret_cast<VkPhysicalDevice>(&device); return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice, VkPhysicalDeviceProperties* out) {
    std::memset(out, 0, sizeof(*out)); std::strcpy(out->deviceName, "Synthetic GPU");
}
static void* symbol(void*, const char* name) {
    if (!std::strcmp(name, "vkCreateInstance")) return reinterpret_cast<void*>(createInstance);
    if (!std::strcmp(name, "vkDestroyInstance")) return reinterpret_cast<void*>(destroyInstance);
    if (!std::strcmp(name, "vkEnumeratePhysicalDevices")) return reinterpret_cast<void*>(enumerate);
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties")) return reinterpret_cast<void*>(properties);
    return nullptr;
}
const GpuInfoList& globalGpuInfoList() { ++waits; return globalList; }
SelectedRenderer emuglConfig_get_current_renderer() { return SELECTED_RENDERER_HOST; }
bool isHostGpuBlacklisted() { return false; }
bool async_query_host_gpu_VulkanBlacklisted() { return false; }
const gfxstream::RendererPtr& android_getOpenglesRenderer() {
    static auto owner = std::make_shared<int>(0);
    static gfxstream::RendererPtr renderer(owner, reinterpret_cast<gfxstream::Renderer*>(owner.get()));
    return renderer; // Stops the old code before any renderer library is loaded.
}
#if !defined(RME_TEST_REAL_PRIMARY)
static int create(uint32_t*) { return -1; }
static int destroy(uint32_t) { return -1; }
static int poseSet(uint32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t) { return -1; }
static int poseGet(uint32_t, int32_t*, int32_t*, uint32_t*, uint32_t*) { return -1; }
static int colorGet(uint32_t, uint32_t*) { return -1; }
static int colorSet(uint32_t, uint32_t) { return -1; }
static bool multi(uint32_t, int32_t*, int32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, bool*) { return false; }
static bool next(int32_t, uint32_t*, int32_t*, int32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*) { return false; }
static bool no() { return false; }
static bool area(int*, int*, int*, int*) { return false; }
static void snapshot(bool) {}
static QAndroidMultiDisplayAgent display{};
namespace rme {
const QAndroidMultiDisplayAgent* primaryDisplayAgent() { return &display; }
bool primaryColorBuffersBound(uint64_t, uint64_t) { return true; }
bool quiescePrimaryColorBuffers(uint64_t, uint64_t) { return true; }
bool clearPrimaryDisplay(uint64_t, uint64_t) { return true; }
}
#endif
int main() {
#if defined(RME_TEST_REAL_PRIMARY)
    auto token = rme::bindPrimaryDisplay(1, 640, 480, 160, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    auto vmToken = rme::bindSnapshotVm(1, rme::SnapshotCapability::Unavailable);
    assert(token && vmToken && rme::enablePrimaryColorBuffers(token, 1));
    gfxstream::host::FeatureSet features{};
    rme::RendererStart params{640, 480, true, 36, rme::snapshotVmAgent(), rme::primaryWindowAgent(),
                              rme::primaryDisplayAgent(), &features, token, 1};
#else
    display.createDisplay = create; display.destroyDisplay = destroy;
    display.setDisplayPose = poseSet; display.getDisplayPose = poseGet;
    display.setDisplayColorBuffer = colorSet; display.getDisplayColorBuffer = colorGet;
    display.getColorBufferDisplay = colorGet; display.getMultiDisplay = multi;
    display.getNextMultiDisplay = next; display.isMultiDisplayEnabled = no; display.isPixelFold = no;
    QAndroidEmulatorWindowAgent window{}; window.isFolded = no; window.getFoldedArea = area;
    QAndroidVmOperations vm{}; vm.setSkipSnapshotSave = snapshot;
    gfxstream::host::FeatureSet features{};
    rme::RendererStart params{640, 480, true, 36, &vm, &window, &display, &features, 1, 1};
#endif
    assert(rme::startRenderer(params) == rme::RendererStartResult::GpuRejected);
    assert(waits == 1);
    rme::DynamicLoaderFunctions loader{openLibrary, symbol, closeLibrary};
    noLibrary = true;
    assert(rme::queryNativeGpuInfoWithDynamicLoader(loader, &globalList).status == rme::GpuInfoStatus::LoaderUnavailable);
    assert(rme::startRenderer(params) == rme::RendererStartResult::GpuRejected);
    noLibrary = false;
    assert(rme::queryNativeGpuInfoWithDynamicLoader(loader, &anotherList).status == rme::GpuInfoStatus::Ok);
    assert(rme::startRenderer(params) == rme::RendererStartResult::GpuRejected);
    assert(rme::queryNativeGpuInfoWithDynamicLoader(loader, &globalList).status == rme::GpuInfoStatus::Ok);
    assert(rme::startRenderer(params) == rme::RendererStartResult::AlreadyAttempted);
#if defined(RME_TEST_REAL_PRIMARY)
    assert(rme::quiescePrimaryColorBuffers(token, 1));
    assert(rme::clearPrimaryDisplay(token, 1));
    assert(rme::clearSnapshotVm(vmToken, 1));
    std::puts("REAL_PRIMARY_AGENT_REACHES_GPU_GATE_OK");
#endif
    std::puts("RENDERER_REJECTS_UNAVAILABLE_OR_UNRELATED_GPU_QUERY_OK");
}
