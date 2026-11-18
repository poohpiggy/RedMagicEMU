#include "primary_display.h"
#include "host-common/multi_display_agent.h"
#include "host-common/window_agent.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

// Extracted verbatim from the actual NX809J HWC; this test invokes query only.
struct hwc { int active_config = 0; };
#define AEMU_NX809J_DYNAMIC_REFRESH 1
static const int refresh_periods[] = {16666666};
#include "hwc_query.inc"

int main() {
    auto* agent = rme::primaryDisplayAgent();
    assert(agent && agent->getMultiDisplay);
    assert(!rme::primaryWindowAgent());
    assert(!rme::bindPrimaryDisplay(1, 540, 960, 240, rme::DisplayTopology::SinglePrimary, rme::FoldMode::Unknown));
    assert(!rme::bindPrimaryDisplay(1, 540, 960, 240, rme::DisplayTopology::SinglePrimary, rme::FoldMode::Foldable));
    assert(!agent->isPixelFold);
    assert(!rme::bindPrimaryDisplay(1, 540, 960, 240, rme::DisplayTopology::PixelFold, rme::FoldMode::NonFoldable));
    assert(!rme::primaryDisplayAgent()->isPixelFold);
    int display_count = 0;
    hwc composer;
    assert(hwc_query(&composer, 2, &display_count) == 0 && display_count == 1);
    assert(agent->isMultiDisplayEnabled && !agent->isMultiDisplayEnabled());
    assert(agent->getNextMultiDisplay);
    uint32_t width = 0, height = 0, dpi = 0;
    assert(!agent->getMultiDisplay(0, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(!rme::bindPrimaryDisplay(0, 540, 960, 240, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable));
    uint64_t token = rme::bindPrimaryDisplay(1, 540, 960, 240, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    assert(token);
    const auto* window = rme::primaryWindowAgent();
    assert(window && window->isFolded && !window->isFolded() && window->getFoldedArea);
    int x = 11, y = 22, w = 33, h = 44;
    assert(!window->getFoldedArea(&x, &y, &w, &h));
    assert(x == 11 && y == 22 && w == 33 && h == 44);
    agent = rme::primaryDisplayAgent();
    assert(agent->isPixelFold && !agent->isPixelFold());
    assert(!rme::bindPrimaryDisplay(2, 540, 960, 240, rme::DisplayTopology::PixelFold, rme::FoldMode::NonFoldable));
    assert(!agent->isPixelFold());
    assert(agent->getMultiDisplay(0, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(width == 540 && height == 960 && dpi == 240);
    assert(agent->createDisplay && agent->destroyDisplay && agent->getDisplayPose && agent->setDisplayPose);
    uint32_t primary = 0, unsupported = 1;
    assert(agent->createDisplay(&primary) == 0 && primary == 0);
    assert(agent->createDisplay(nullptr) < 0 && agent->createDisplay(&unsupported) < 0 && unsupported == 1);
    assert(agent->destroyDisplay(0) < 0 && agent->destroyDisplay(1) < 0);
    int32_t poseX = 31, poseY = 32;
    assert(agent->getDisplayPose(0, &poseX, &poseY, &width, &height) < 0);
    assert(poseX == 31 && poseY == 32);
    assert(agent->setDisplayPose(0, 0, 0, 540, 960, 0) == 0);
    assert(agent->getMultiDisplay(0, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(width == 540 && height == 960 && dpi == 0);
    assert(agent->setDisplayPose(0, 0, 0, 540, 960, 240) == 0);
    assert(agent->getDisplayPose(0, &poseX, &poseY, &width, &height) == 0);
    assert(poseX == 0 && poseY == 0 && width == 540 && height == 960);
    assert(agent->setDisplayPose(0, 5, 0, 540, 960, 240) < 0);
    assert(agent->setDisplayPose(0, 0, 0, 541, 960, 240) < 0);
    assert(agent->getDisplayPose(1, &poseX, &poseY, &width, &height) < 0);
    uint32_t id = 99;
    assert(agent->getNextMultiDisplay(-1, &id, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(id == 0 && width == 540 && height == 960 && dpi == 240);
    assert(!agent->getNextMultiDisplay(0, &id, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    uint32_t unknown_buffer = 42;
    assert(!agent->getNextMultiDisplay(-1, &id, nullptr, nullptr, &width, &height, &dpi, nullptr, &unknown_buffer));
    assert(unknown_buffer == 42);
    assert(!agent->getMultiDisplay(1, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    uint32_t unknown_flags = 7;
    assert(!agent->getMultiDisplay(0, nullptr, nullptr, nullptr, nullptr, nullptr, &unknown_flags, nullptr));
    assert(unknown_flags == 7);
    assert(!rme::updatePrimaryDisplay(token, 2, 1080, 1920, 480));
    assert(rme::updatePrimaryDisplay(token, 1, 1080, 1920, 480));
    assert(agent->getMultiDisplay(0, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(width == 1080 && height == 1920 && dpi == 480);
    assert(!rme::clearPrimaryDisplay(token, 2));
    assert(rme::clearPrimaryDisplay(token, 1));
    assert(!rme::primaryWindowAgent());
    assert(!agent->getMultiDisplay(0, nullptr, nullptr, &width, &height, &dpi, nullptr, nullptr));
    assert(agent->createDisplay(&primary) < 0 && agent->getDisplayPose(0, nullptr, nullptr, nullptr, nullptr) < 0);
    assert(!rme::primaryDisplayAgent()->isPixelFold);
    uint64_t next = rme::bindPrimaryDisplay(1, 320, 640, 160, rme::DisplayTopology::SinglePrimary, rme::FoldMode::NonFoldable);
    assert(next && next != token && !rme::clearPrimaryDisplay(token, 1));
    assert(rme::clearPrimaryDisplay(next, 1));
    assert(!agent->getDisplayColorBuffer && !agent->setDisplayColorBuffer);
    std::puts("PRIMARY_DISPLAY_SINGLE_DISPLAY_POSE_OK");
}
