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

/* Submit carries a VIDEO_SUBMIT header and waits for an available codec input
 * buffer before writing packed NV12. Receive returns one encoded access unit.
 * An end-of-stream submit has no image payload. */
static inline int camera_encode_dimensions(const struct camera_encode_open *open) {
    return open->magic == RME_CAMERA_ENCODE_MAGIC &&
           video_dimensions(open->width, open->height) &&
           (open->fps == 50 || open->fps == 60 || open->fps == 120) &&
           open->bitrate >= 64000 && open->bitrate <= 128000000;
}
