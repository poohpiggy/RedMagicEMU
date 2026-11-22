#include "video_client.h"

#include <string.h>
#include <sys/time.h>
#include <sys/un.h>

int rme_video_open(const char *path, const struct video_open *config, const void *csd0,
                   const void *csd1, struct video_format *format) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof address.sun_path ||
        config->magic != RME_VIDEO_MAGIC || !video_open_dimensions(config) ||
        !video_mime(config->codec) || config->csd0_size > RME_VIDEO_MAX_CONFIG ||
        config->csd1_size > RME_VIDEO_MAX_CONFIG || (config->csd0_size && !csd0) ||
        (config->csd1_size && !csd1)) return -1;
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 10};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) != 0 ||
        connect(fd, (struct sockaddr *)&address, sizeof address) != 0 ||
        video_write(fd, config, sizeof *config) != 0 ||
        video_write(fd, csd0, config->csd0_size) != 0 ||
        video_write(fd, csd1, config->csd1_size) != 0 ||
        video_read(fd, format, sizeof *format) != 0 || format->status != VIDEO_OK ||
        !memchr(format->decoder, '\0', sizeof format->decoder)) {
        close(fd);
        return -1;
    }
    return fd;
}

static uint32_t transact(int fd, const struct video_packet *command, const void *data,
                         void *frame, size_t capacity, struct video_packet *response) {
    if (video_write(fd, command, sizeof *command) != 0 ||
        video_write(fd, data, command->size) != 0 || video_read(fd, response, sizeof *response) != 0 ||
        response->size > capacity || response->type > VIDEO_ERROR) return VIDEO_ERROR;
    if (response->size) {
        uint32_t width, height;
        if (command->type != VIDEO_RECEIVE || response->type != VIDEO_OK ||
            !video_frame_dimensions(response, &width, &height) ||
            video_read(fd, frame, response->size) != 0) return VIDEO_ERROR;
    } else if (response->reserved) {
        return VIDEO_ERROR;
    }
    return response->type;
}

uint32_t rme_video_submit(int fd, const void *compressed, uint32_t size, int64_t pts, uint32_t flags) {
    if (size > RME_VIDEO_MAX_SAMPLE || (size && !compressed)) return VIDEO_ERROR;
    struct video_packet command = {.type = VIDEO_SUBMIT, .flags = flags, .size = size, .pts_us = pts};
    struct video_packet response;
    return transact(fd, &command, compressed, NULL, 0, &response);
}

uint32_t rme_video_receive(int fd, void *frame, size_t capacity, struct video_packet *packet) {
    const struct video_packet command = {.type = VIDEO_RECEIVE};
    return transact(fd, &command, NULL, frame, capacity, packet);
}

uint32_t rme_video_flush(int fd) {
    const struct video_packet command = {.type = VIDEO_FLUSH};
    struct video_packet response;
    return transact(fd, &command, NULL, NULL, 0, &response);
}
