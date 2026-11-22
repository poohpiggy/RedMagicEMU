#pragma once

#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

/* Private local protocol between ARM64 processes on the same host. */
#define RME_VIDEO_MAGIC UINT32_C(0x524d5632)
#define RME_VIDEO_MAX_CONFIG (64u * 1024u)
#define RME_VIDEO_MAX_SAMPLE (16u * 1024u * 1024u)
#define RME_VIDEO_MAX_WIDTH 4096u
#define RME_VIDEO_MAX_HEIGHT 2160u

enum { VIDEO_AVC = 1, VIDEO_HEVC = 2 };
enum { VIDEO_SUBMIT = 1, VIDEO_RECEIVE = 2, VIDEO_FLUSH = 3 };
enum { VIDEO_OK = 0, VIDEO_AGAIN = 1, VIDEO_ERROR = 2 };
enum { VIDEO_SYNC = 1, VIDEO_CONFIG = 2, VIDEO_EOS = 4 };

struct video_open {
    uint32_t magic, codec, width, height, csd0_size, csd1_size;
    uint32_t max_width, max_height;  /* both zero for fixed-size decoding */
};

struct video_packet {
    int64_t pts_us;
    /* Only decoder receive replies with frame data use reserved for packed width and height. */
    uint32_t type, flags, size, reserved;
};

struct video_format {
    char decoder[96];
    uint32_t status;
};

static inline int video_read(int fd, void *buf, size_t size) {
    char *data = (char *)buf;
    while (size > 0) {
        ssize_t received = read(fd, data, size);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return -1;
        data += received;
        size -= (size_t)received;
    }
    return 0;
}

static inline int video_write(int fd, const void *buf, size_t size) {
    const char *data = (const char *)buf;
    while (size > 0) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        data += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static inline const char *video_mime(uint32_t codec) {
    if (codec == VIDEO_AVC) return "video/avc";
    if (codec == VIDEO_HEVC) return "video/hevc";
    return NULL;
}

static inline int video_dimensions(uint32_t width, uint32_t height) {
    return width && height && width <= RME_VIDEO_MAX_WIDTH && height <= RME_VIDEO_MAX_HEIGHT &&
           (width % 2 == 0) && (height % 2 == 0);
}

static inline int video_open_dimensions(const struct video_open *open) {
    return video_dimensions(open->width, open->height) &&
           ((!open->max_width && !open->max_height) ||
            (video_dimensions(open->max_width, open->max_height) &&
             open->width <= open->max_width && open->height <= open->max_height));
}

static inline int video_output_dimensions(const struct video_open *open, uint32_t width, uint32_t height) {
    return video_dimensions(width, height) &&
           (open->max_width ? (width <= open->max_width && height <= open->max_height) :
            (width == open->width && height == open->height));
}

static inline size_t video_frame_size(uint32_t width, uint32_t height) {
    return (size_t)width * height * 3 / 2;
}

static inline int video_frame_dimensions(const struct video_packet *packet, uint32_t *width, uint32_t *height) {
    *width = packet->reserved >> 16;
    *height = packet->reserved & UINT32_C(0xffff);
    return video_dimensions(*width, *height) && packet->size == video_frame_size(*width, *height);
}
