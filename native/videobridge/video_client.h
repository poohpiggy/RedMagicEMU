#pragma once

#include "video_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

int rme_video_open(const char *path, const struct video_open *config, const void *csd0,
                   const void *csd1, struct video_format *format);
uint32_t rme_video_submit(int fd, const void *compressed, uint32_t size, int64_t pts, uint32_t flags);
uint32_t rme_video_receive(int fd, void *frame, size_t capacity, struct video_packet *packet);
uint32_t rme_video_flush(int fd);

#ifdef __cplusplus
}
#endif
