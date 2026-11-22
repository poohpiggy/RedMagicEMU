#pragma once

#include "video_wire.h"

#define RME_CAMERA_ENCODE_MAGIC UINT32_C(0x524d4531)
#define RME_CAMERA_ENCODE_SOCKET "rme-camera-encode"

struct camera_encode_open {
    uint32_t magic, width, height, fps, bitrate;
};

struct camera_encode_format {
    char encoder[96];
    uint32_t status;
};

static inline int camera_encode_frame_rate_supported(float fps) {
    return fps == 30.0f || fps == 50.0f || fps == 60.0f || fps == 120.0f;
}

/* Submit carries a VIDEO_SUBMIT header and waits for an available codec input
 * buffer before writing packed NV12. Receive returns one encoded access unit.
 * An end-of-stream submit has no image payload. */
static inline int camera_encode_dimensions(const struct camera_encode_open *open) {
    return open->magic == RME_CAMERA_ENCODE_MAGIC &&
           video_dimensions(open->width, open->height) &&
           camera_encode_frame_rate_supported(open->fps) &&
           open->bitrate >= 64000 && open->bitrate <= 128000000;
}
