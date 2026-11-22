#include "../camera_encode_chroma.h"

#include <array>
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr C2Rect uncropped(6, 4);
constexpr std::array<uint8_t, 12> expected = {11, 21, 12, 22, 13, 23, 14, 24, 15, 25, 16, 26};

C2PlaneInfo plane(C2PlaneInfo::channel_t channel, int col, int row, unsigned sampling,
                 unsigned root, unsigned offset) {
    return {channel, col, row, sampling, sampling, 8, 8, 0, C2PlaneInfo::NATIVE, root, offset};
}

C2PlanarLayout nv12(int stride) {
    return {C2PlanarLayout::TYPE_YUV, 3, 2,
            {plane(C2PlaneInfo::CHANNEL_Y, 1, 8, 1, 0, 0),
             plane(C2PlaneInfo::CHANNEL_CB, 2, stride, 2, 1, 0),
             plane(C2PlaneInfo::CHANNEL_CR, 2, stride, 2, 1, 1), {}}};
}

C2PlanarLayout nv21(int stride) {
    auto layout = nv12(stride);
    layout.planes[0].rowInc = stride;
    layout.planes[1].rootIx = 2;
    layout.planes[1].offset = 1;
    layout.planes[2].rootIx = 2;
    layout.planes[2].offset = 0;
    return layout;
}

void check(const C2PlanarLayout &layout, const uint8_t *u, const uint8_t *v,
           CameraChromaCopy expectedPath, const C2Rect &crop = uncropped) {
    std::array<uint8_t, expected.size() + 2> destination{};
    const auto path = copyCameraChroma(destination.data() + 1, u, v, layout, crop, 6, 4);
    assert(std::memcmp(destination.data() + 1, expected.data(), expected.size()) == 0);
    assert(destination.front() == 0 && destination.back() == 0);
    assert(path == expectedPath && "select the chroma operation matching the mapped planes");
}

void interleavedNv12() {
    check(nv12(6), expected.data(), expected.data() + 1, CameraChromaCopy::Nv12Rows);

    // A larger odd crop with padded rows still yields the configured 6x4 image.
    const uint8_t padded[] = {11, 21, 12, 22, 13, 23, 90, 91, 92,
                              14, 24, 15, 25, 16, 26, 93, 94, 95,
                              81, 82, 83, 84, 85, 86, 96, 97, 98};
    check(nv12(9), padded, padded + 1, CameraChromaCopy::Nv12Rows, C2Rect(7, 5));
    check(nv12(9), padded, padded + 1, CameraChromaCopy::Repacked, C2Rect(6, 4).at(2, 2));
    check(nv12(9), padded, padded + 1, CameraChromaCopy::Repacked, C2Rect(5, 4));
    check(nv12(9), padded, padded + 1, CameraChromaCopy::Repacked, C2Rect(6, 3));
}

void checkNv21Rows(uint32_t width, uint32_t height, uint32_t stride) {
    const uint32_t rows = height / 2;
    const size_t sourceSize = (rows - 1) * stride + width;
    const long pageSize = sysconf(_SC_PAGESIZE);
    assert(pageSize > 0);
    const size_t dataPages = (sourceSize + pageSize - 1) / pageSize * pageSize;
    const size_t mapSize = dataPages + pageSize;
    void *mapping = mmap(nullptr, mapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    auto *guard = static_cast<uint8_t *>(mapping) + dataPages;
    assert(mprotect(guard, pageSize, PROT_NONE) == 0);
    auto *source = guard - sourceSize;
    std::memset(source, 0xee, sourceSize);
    std::vector<uint8_t> packed(width * rows + 2, 0xee);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t x = 0; x < width; x += 2) {
            source[row * stride + x] = static_cast<uint8_t>(21 + row + x);
            source[row * stride + x + 1] = static_cast<uint8_t>(11 + 3 * row + x);
        }
    }

    const auto path = copyCameraChroma(packed.data() + 1, source + 1, source, nv21(stride),
                                       C2Rect(width, height), width, height);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t x = 0; x < width; x += 2) {
            assert(packed[row * width + x + 1] == static_cast<uint8_t>(11 + 3 * row + x));
            assert(packed[row * width + x + 2] == static_cast<uint8_t>(21 + row + x));
        }
    }
    assert(packed.front() == 0xee && packed.back() == 0xee);
    assert(munmap(mapping, mapSize) == 0);
    assert(path == CameraChromaCopy::Nv21Swap && "packed NV21 should swap adjacent byte pairs");
}

void mismatchedNv21() {
    const uint8_t vu[] = {21, 11, 22, 12, 23, 13, 24, 14, 25, 15, 26, 16};
    auto layout = nv21(6);
    layout.planes[1].offset = 0;
    check(layout, vu + 1, vu, CameraChromaCopy::Repacked);

    layout = nv21(6);
    layout.planes[2].rootIx = 1;
    check(layout, vu + 1, vu, CameraChromaCopy::Repacked);

    layout = nv21(6);
    layout.planes[2].colSampling = 1;
    check(layout, vu + 1, vu, CameraChromaCopy::Repacked);

    check(nv12(6), vu + 1, vu, CameraChromaCopy::Repacked);
    check(nv21(6), expected.data(), expected.data() + 1, CameraChromaCopy::Repacked);
}

void paddedPlanar() {
    const uint8_t u[] = {11, 12, 13, 91, 14, 15, 16};
    const uint8_t v[] = {21, 22, 23, 92, 93, 24, 25, 26};
    auto layout = nv12(6);
    layout.rootPlanes = 3;
    layout.planes[1] = plane(C2PlaneInfo::CHANNEL_CB, 1, 4, 2, 1, 0);
    layout.planes[2] = plane(C2PlaneInfo::CHANNEL_CR, 1, 5, 2, 2, 0);
    check(layout, u, v, CameraChromaCopy::Repacked);
}

void unconventionalChroma() {
    auto layout = nv12(6);
    layout.planes[2].rootIx = 2;
    check(layout, expected.data(), expected.data() + 1, CameraChromaCopy::Repacked);

    layout = nv12(6);
    layout.planes[1].rowSampling = 1;
    check(layout, expected.data(), expected.data() + 1, CameraChromaCopy::Repacked);

    layout = nv12(6);
    layout.planes[2].bitDepth = 7;
    check(layout, expected.data(), expected.data() + 1, CameraChromaCopy::Repacked);

    const uint8_t overlappingRows[] = {11, 21, 12, 22, 13, 23, 24, 15, 25, 16, 26};
    std::array<uint8_t, expected.size()> output{};
    const std::array<uint8_t, expected.size()> overlappingExpected = {
        11, 21, 12, 22, 13, 23, 23, 24, 15, 25, 16, 26};
    assert(copyCameraChroma(output.data(), overlappingRows, overlappingRows + 1,
                            nv12(5), uncropped, 6, 4) == CameraChromaCopy::Repacked);
    assert(output == overlappingExpected);

    const uint8_t differentStrides[] = {11, 21, 12, 22, 13, 23, 14, 90, 15, 24, 16, 25, 91, 26};
    layout = nv12(6);
    layout.planes[2].rowInc = 8;
    check(layout, differentStrides, differentStrides + 1, CameraChromaCopy::Repacked);
}

} // namespace

int main() {
    interleavedNv12();
    checkNv21Rows(1920, 1080, 1920);
    checkNv21Rows(16, 4, 16);
    checkNv21Rows(6, 4, 9);
    checkNv21Rows(34, 4, 39);
    mismatchedNv21();
    paddedPlanar();
    unconventionalChroma();
}
