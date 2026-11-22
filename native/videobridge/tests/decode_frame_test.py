"""Run the real host frame-copy and reply path against synthetic NDK images."""

from pathlib import Path
import os
import subprocess
import tempfile


bridge = Path(__file__).resolve().parents[1]
source = (bridge / "host_decode.c").read_text()

stubs = r"""
#include "video_wire.h"
#include "video_client.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int media_status_t;
typedef void AMediaCodec;
typedef void AImageReader;
typedef struct { int32_t left, top, right, bottom; } AImageCropRect;
typedef struct {
    AImageCropRect crop;
    uint8_t *planes[3];
    int32_t row_stride[3], size[3];
} AImage;
typedef struct { int64_t presentationTimeUs; int32_t size; uint32_t flags; } AMediaCodecBufferInfo;
enum {
    AMEDIA_OK = 0, AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1,
    AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2, AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3,
    AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG = 2,
};
static AImage image;
static int AImage_getCropRect(AImage *src, AImageCropRect *crop) {
    *crop = src->crop;
    return AMEDIA_OK;
}
static int AImage_getPlaneData(AImage *src, int plane, uint8_t **data, int32_t *size) {
    *data = src->planes[plane];
    *size = src->size[plane];
    return AMEDIA_OK;
}
static int AImage_getPlanePixelStride(AImage *src, int plane, int32_t *stride) {
    (void)src;
    *stride = plane ? 2 : 1;
    return AMEDIA_OK;
}
static int AImage_getPlaneRowStride(AImage *src, int plane, int32_t *stride) {
    *stride = src->row_stride[plane];
    return AMEDIA_OK;
}
static ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info,
                                               int64_t timeout) {
    (void)codec;
    (void)timeout;
    *info = (AMediaCodecBufferInfo){.presentationTimeUs = 123, .size = 1};
    return 0;
}
static int AMediaCodec_releaseOutputBuffer(AMediaCodec *codec, size_t slot, bool render) {
    (void)codec;
    return slot == 0 && render ? AMEDIA_OK : -1;
}
"""

cases = r"""
static int acquire_frame(struct decoder *decoder) {
    return copy_frame(decoder, &image);
}
"""

exercise = r"""
static uint32_t frame_crc(const uint8_t *pixels, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= pixels[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static int check_frame(struct decoder *decoder, int width, int height,
                       uint32_t expected_status, uint32_t expected_crc) {
    image.crop = (AImageCropRect){2, 2, width + 2, height + 2};
    for (int plane = 0; plane < 3; ++plane) {
        const int shift = plane ? 1 : 0;
        const int pixel_stride = plane ? 2 : 1;
        image.row_stride[plane] = (width >> shift) * pixel_stride + 16;
        image.size[plane] = image.row_stride[plane] * ((height + 2) >> shift);
        image.planes[plane] = calloc(1, image.size[plane]);
        if (!image.planes[plane]) abort();
        for (int row = 0; row < (height >> shift); ++row) {
            uint8_t *out = image.planes[plane] + (row + (2 >> shift)) * image.row_stride[plane]
                           + (2 >> shift) * pixel_stride;
            for (int col = 0; col < (width >> shift); ++col) {
                out[col * pixel_stride] = (uint8_t)(3 * row + col + 7 * plane);
            }
        }
    }
    struct video_packet reply = {0};
    uint32_t status = receive(decoder, &reply);
    for (int plane = 0; plane < 3; ++plane) free(image.planes[plane]);
    if (status != expected_status) {
        fprintf(stderr, "%dx%d: expected status %u, got %u\n", width, height, expected_status, status);
        return 1;
    }
    if (status != VIDEO_OK) return reply.size != 0;
    if (reply.pts_us != 123 || reply.size != (uint32_t)(width * height * 3 / 2) ||
        frame_crc(decoder->frame, reply.size) != expected_crc) {
        fprintf(stderr, "%dx%d: incorrect frame payload\n", width, height);
        return 1;
    }
    if (reply.reserved != ((uint32_t)width << 16 | (uint32_t)height)) {
        fprintf(stderr, "%dx%d: incorrect frame metadata\n", width, height);
        return 1;
    }
    return 0;
}

static int check_client(uint32_t geometry, uint32_t size, uint32_t expected) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return 1;
    const struct video_packet packet = {.type = VIDEO_OK, .size = size, .reserved = geometry};
    uint8_t payload[384] = {0};
    payload[383] = 23;
    if (video_write(sockets[0], &packet, sizeof packet) != 0 ||
        video_write(sockets[0], payload, sizeof payload) != 0) abort();
    struct video_packet reply;
    uint8_t frame[384] = {0};
    const uint32_t status = rme_video_receive(sockets[1], frame, sizeof frame, &reply);
    close(sockets[0]);
    close(sockets[1]);
    if (status != expected || frame[383] != (expected == VIDEO_OK ? 23 : 0)) {
        fprintf(stderr, "decoder client failed geometry/size validation\n");
        return 1;
    }
    return 0;
}

int main(void) {
    struct decoder decoder = {.params = {.width = 1280, .height = 720,
                                         .max_width = 1920, .max_height = 1080}};
    unsigned failed = check_frame(&decoder, 1280, 720, VIDEO_OK, 0x29fe79df);
    failed += check_frame(&decoder, 1920, 1080, VIDEO_OK, 0x5f1c2e5f);
    failed += check_frame(&decoder, 1280, 720, VIDEO_OK, 0x29fe79df);
    failed += check_frame(&decoder, 1922, 1080, VIDEO_ERROR, 0);
    decoder.params.max_width = decoder.params.max_height = 0;
    failed += check_frame(&decoder, 1280, 720, VIDEO_OK, 0x29fe79df);
    failed += check_frame(&decoder, 1920, 1080, VIDEO_ERROR, 0);
    free(decoder.frame);
    failed += check_client(16 << 16 | 16, 384, VIDEO_OK);
    failed += check_client(16 << 16 | 16, 383, VIDEO_ERROR);
    failed += check_client(0, 384, VIDEO_ERROR);
    return failed != 0;
}
"""

fragments = (
    source[source.index("struct decoder {"):source.index("static media_status_t (*next_decoder)")],
    source[source.index("static int copy_frame("):source.index("static int acquire_frame(")],
    cases,
    source[source.index("static uint32_t receive("):source.index("static void session(")],
)

with tempfile.TemporaryDirectory(prefix="rme-decode-frame-test-") as temp:
    test = Path(temp) / "decode_frame_test.c"
    executable = test.with_suffix("")
    test.write_text(stubs + "\n".join(fragments) + exercise)
    subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I", str(bridge), str(test), str(bridge / "video_client.c"), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
