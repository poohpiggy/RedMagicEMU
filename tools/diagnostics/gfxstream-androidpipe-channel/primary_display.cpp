#include "primary_display.h"
#include "host_color_registry.h"
#include "host-common/multi_display_agent.h"
#include "host-common/window_agent.h"
#include <mutex>
#include <memory>

namespace rme {
namespace {
std::mutex mutex;
uint64_t currentGeneration = 0;
uint64_t currentToken = 0, nextToken = 0;
uint32_t currentWidth = 0, currentHeight = 0, currentDensity = 0;
bool primaryPoseKnown = false;
DisplayTopology currentTopology = DisplayTopology::SinglePrimary;
FoldMode currentFoldMode = FoldMode::Unknown;
bool colorBuffersEnabled = false;
bool colorBuffersClosing = false;
uint32_t colorCallbacksInFlight = 0;
uint32_t currentColorBuffer = 0;
std::weak_ptr<void> currentColorRef;
class ColorLease {
public:
    ColorLease() {
        std::lock_guard<std::mutex> guard(mutex);
        if (currentGeneration && colorBuffersEnabled && !colorBuffersClosing) {
            ++colorCallbacksInFlight;
            held = true;
        }
    }
    ~ColorLease() {
        if (!held) return;
        std::lock_guard<std::mutex> guard(mutex);
        --colorCallbacksInFlight;
    }
    explicit operator bool() const { return held; }
private:
    bool held = false;
};
bool sameOwner(const std::weak_ptr<void>& a, const std::weak_ptr<void>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}
bool dimensionsValid(uint64_t generation, uint32_t width, uint32_t height, uint32_t density) {
    return generation && width && height && density;
}
bool getDisplay(uint32_t id, int32_t* x, int32_t* y, uint32_t* width, uint32_t* height,
                uint32_t* density, uint32_t* flags, bool* enabled) {
    // The current source supplies only the primary display's dimensions and
    // density. Do not invent position, flags, or enabled state.
    if (id != 0 || x || y || flags || enabled) return false;
    std::lock_guard<std::mutex> guard(mutex);
    if (!currentGeneration) return false;
    if (width) *width = currentWidth;
    if (height) *height = currentHeight;
    if (density) *density = currentDensity;
    return true;
}
bool isMultiDisplayEnabled() {
    // The target NX809J HWC reports exactly one primary display.
    return false;
}
bool isPixelFold() {
    std::lock_guard<std::mutex> guard(mutex);
    return currentGeneration && currentTopology == DisplayTopology::PixelFold;
}
bool isFolded() {
    std::lock_guard<std::mutex> guard(mutex);
    return currentGeneration && currentFoldMode == FoldMode::Foldable;
}
bool getFoldedArea(int*, int*, int*, int*) {
    // The only accepted mode is explicitly non-foldable and has no area.
    return false;
}
bool getNextDisplay(int32_t start, uint32_t* id, int32_t* x, int32_t* y, uint32_t* width,
                    uint32_t* height, uint32_t* density, uint32_t* flags, uint32_t* colorBuffer) {
    if (start >= 0 || colorBuffer || !getDisplay(0, x, y, width, height, density, flags, nullptr))
        return false;
    if (id) *id = 0;
    return true;
}
int createDisplay(uint32_t* id) {
    if (!id || *id != 0) return -1;
    std::lock_guard<std::mutex> guard(mutex);
    // The primary display is already bound; a repeated create is idempotent.
    return currentGeneration ? 0 : -1;
}
int destroyDisplay(uint32_t) {
    // The EMU lifecycle owns its primary display; extra displays are unsupported.
    return -1;
}
int setDisplayPose(uint32_t id, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t dpi) {
    if (id != 0 || x != 0 || y != 0) return -1;
    std::lock_guard<std::mutex> guard(mutex);
    // Do not claim a resize unless it matches the bound EMU framebuffer.
    if (!currentGeneration || width != currentWidth || height != currentHeight) return -1;
    currentDensity = dpi; // gfxstream display state; zero is a valid upstream input.
    primaryPoseKnown = true;
    return 0;
}
int getDisplayPose(uint32_t id, int32_t* x, int32_t* y, uint32_t* width, uint32_t* height) {
    std::lock_guard<std::mutex> guard(mutex);
    if (id != 0 || !currentGeneration || !primaryPoseKnown) return -1;
    if (x) *x = 0;
    if (y) *y = 0;
    if (width) *width = currentWidth;
    if (height) *height = currentHeight;
    return 0;
}
int setDisplayColorBuffer(uint32_t id, uint32_t handle) {
    if (id != 0) return -1;
    ColorLease lease;
    if (!lease) return -1;
    uint64_t token, generation;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!currentGeneration || !colorBuffersEnabled || colorBuffersClosing) return -1;
        token = currentToken;
        generation = currentGeneration;
        if (!handle) {
            currentColorBuffer = 0;
            currentColorRef.reset();
        }
    }
    if (!handle) return 0;
    auto found = findRegisteredColorBuffer(handle);
    if (!found) return -1;
    auto expected = found;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!colorBuffersEnabled || colorBuffersClosing || currentToken != token || currentGeneration != generation) return -1;
        currentColorRef = found;
        currentColorBuffer = handle;
    }
    // The upstream registry can drop or reuse the numeric handle during the
    // lookup. Verify the same object remains registered after association.
    auto confirmed = findRegisteredColorBuffer(handle);
    if (!confirmed || confirmed.get() != expected.get()) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (currentToken == token && currentGeneration == generation &&
                currentColorBuffer == handle && sameOwner(currentColorRef, expected)) {
                currentColorBuffer = 0;
                currentColorRef.reset();
            }
        }
        return -1;
    }
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!colorBuffersEnabled || colorBuffersClosing || currentToken != token || currentGeneration != generation ||
            currentColorBuffer != handle || !sameOwner(currentColorRef, expected)) return -1;
    }
    return 0;
}
int getDisplayColorBuffer(uint32_t id, uint32_t* output) {
    if (id != 0 || !output) return -1;
    ColorLease lease;
    if (!lease) return -1;
    uint32_t handle;
    uint64_t token, generation;
    std::weak_ptr<void> expectedWeak;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!currentGeneration || !colorBuffersEnabled || colorBuffersClosing) return -1;
        handle = currentColorBuffer;
        if (!handle) { *output = 0; return 0; }
        expectedWeak = currentColorRef;
        token = currentToken;
        generation = currentGeneration;
    }
    auto expected = expectedWeak.lock();
    if (!expected) return -1;
    auto actual = findRegisteredColorBuffer(handle);
    if (!actual || actual.get() != expected.get()) return -1;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!colorBuffersEnabled || colorBuffersClosing || currentToken != token || currentGeneration != generation ||
            currentColorBuffer != handle || !sameOwner(currentColorRef, expectedWeak)) return -1;
        *output = handle;
    }
    return 0;
}
int getColorBufferDisplay(uint32_t handle, uint32_t* output) {
    uint32_t current = 0;
    if (!handle || !output || getDisplayColorBuffer(0, &current) != 0 || current != handle) return -1;
    *output = 0;
    return 0;
}
}
uint64_t bindPrimaryDisplay(uint64_t generation, uint32_t width, uint32_t height, uint32_t density,
                            DisplayTopology topology, FoldMode foldMode) {
    if (!dimensionsValid(generation, width, height, density) || topology != DisplayTopology::SinglePrimary ||
        foldMode != FoldMode::NonFoldable) return 0;
    std::lock_guard<std::mutex> guard(mutex);
    if (currentGeneration || nextToken == UINT64_MAX) return 0;
    currentToken = ++nextToken;
    currentGeneration = generation;
    currentWidth = width;
    currentHeight = height;
    currentDensity = density;
    primaryPoseKnown = false;
    currentTopology = topology;
    currentFoldMode = foldMode;
    return currentToken;
}
bool updatePrimaryDisplay(uint64_t token, uint64_t generation, uint32_t width, uint32_t height, uint32_t density) {
    if (!dimensionsValid(generation, width, height, density)) return false;
    std::lock_guard<std::mutex> guard(mutex);
    if (!token || currentToken != token || currentGeneration != generation) return false;
    currentWidth = width;
    currentHeight = height;
    currentDensity = density;
    return true;
}
bool clearPrimaryDisplay(uint64_t token, uint64_t generation) {
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!token || !generation || currentToken != token || currentGeneration != generation) return false;
        if (colorBuffersEnabled && (!colorBuffersClosing || colorCallbacksInFlight)) return false;
        currentGeneration = 0;
        currentToken = 0;
        currentFoldMode = FoldMode::Unknown;
        currentWidth = currentHeight = currentDensity = 0;
        primaryPoseKnown = false;
        colorBuffersEnabled = false;
        colorBuffersClosing = false;
        currentColorBuffer = 0;
        currentColorRef.reset();
    }
    return true;
}
bool enablePrimaryColorBuffers(uint64_t token, uint64_t generation) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!token || !generation || currentToken != token || currentGeneration != generation || colorBuffersEnabled) return false;
    colorBuffersEnabled = true;
    colorBuffersClosing = false;
    return true;
}
bool primaryColorBuffersBound(uint64_t token, uint64_t generation) {
    std::lock_guard<std::mutex> guard(mutex);
    return token && generation && currentToken == token && currentGeneration == generation && colorBuffersEnabled;
}
bool quiescePrimaryColorBuffers(uint64_t token, uint64_t generation) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!token || !generation || currentToken != token || currentGeneration != generation || !colorBuffersEnabled) return false;
    colorBuffersClosing = true;
    return colorCallbacksInFlight == 0;
}
const QAndroidMultiDisplayAgent* primaryDisplayAgent() {
    static const QAndroidMultiDisplayAgent agent = [] {
        QAndroidMultiDisplayAgent value{};
        value.getMultiDisplay = getDisplay;
        value.getNextMultiDisplay = getNextDisplay;
        value.isMultiDisplayEnabled = isMultiDisplayEnabled;
        return value;
    }();
    static const QAndroidMultiDisplayAgent configuredAgent = [] {
        auto value = agent;
        value.isPixelFold = isPixelFold;
        value.createDisplay = createDisplay;
        value.destroyDisplay = destroyDisplay;
        value.setDisplayPose = setDisplayPose;
        value.getDisplayPose = getDisplayPose;
        return value;
    }();
    static const QAndroidMultiDisplayAgent colorAgent = [] {
        auto value = configuredAgent;
        value.setDisplayColorBuffer = setDisplayColorBuffer;
        value.getDisplayColorBuffer = getDisplayColorBuffer;
        value.getColorBufferDisplay = getColorBufferDisplay;
        return value;
    }();
    std::lock_guard<std::mutex> guard(mutex);
    return !currentGeneration ? &agent : colorBuffersEnabled && !colorBuffersClosing ? &colorAgent : &configuredAgent;
}
const QAndroidEmulatorWindowAgent* primaryWindowAgent() {
    static const QAndroidEmulatorWindowAgent agent = [] {
        QAndroidEmulatorWindowAgent value{};
        value.isFolded = isFolded;
        value.getFoldedArea = getFoldedArea;
        return value;
    }();
    std::lock_guard<std::mutex> guard(mutex);
    return currentGeneration ? &agent : nullptr;
}
}
