#include "video_client.h"

#include <fcntl.h>
#include <media/NdkMediaExtractor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

static int exercise(AMediaExtractor *extractor, const char *socket_path) {
    int result = 1;
    size_t track = 0;
    size_t total_tracks = AMediaExtractor_getTrackCount(extractor);
    struct video_open config = {.magic = RME_VIDEO_MAGIC};
    AMediaFormat *format = NULL;
    uint8_t *sample = NULL, *frame = NULL;
    int connection = -1;
    for (; track < total_tracks; ++track) {
        format = AMediaExtractor_getTrackFormat(extractor, track);
        const char *mime;
        if (format && AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            if (!strcmp(mime, video_mime(VIDEO_AVC))) config.codec = VIDEO_AVC;
            if (!strcmp(mime, video_mime(VIDEO_HEVC))) config.codec = VIDEO_HEVC;
        }
        if (config.codec != 0) break;
        if (format) AMediaFormat_delete(format);
        format = NULL;
    }
    if (!format || AMediaExtractor_selectTrack(extractor, track) != AMEDIA_OK) goto done;
    int32_t width, height;
    if (!AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) ||
        !AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height) ||
        !video_dimensions((uint32_t)width, (uint32_t)height)) goto done;
    config.width = (uint32_t)width;
    config.height = (uint32_t)height;
    void *csd0 = NULL, *csd1 = NULL;
    size_t csd0_size = 0, csd1_size = 0;
    AMediaFormat_getBuffer(format, "csd-0", &csd0, &csd0_size);
    AMediaFormat_getBuffer(format, "csd-1", &csd1, &csd1_size);
    if (csd0_size > RME_VIDEO_MAX_CONFIG || csd1_size > RME_VIDEO_MAX_CONFIG) goto done;
    config.csd0_size = (uint32_t)csd0_size;
    config.csd1_size = (uint32_t)csd1_size;
    struct video_format selected;
    connection = rme_video_open(socket_path, &config, csd0, csd1, &selected);
    if (connection < 0) {
        fprintf(stderr, "Could not open the host decoder\n");
        goto done;
    }
    size_t frame_size = (size_t)width * (size_t)height * 3 / 2;
    frame = malloc(frame_size);
    sample = malloc(RME_VIDEO_MAX_SAMPLE);
    if (!sample || !frame) goto done;
    unsigned frames = 0;
    int input_done = 0, output_done = 0;
    uLong checksum = crc32(0L, Z_NULL, 0);
    uLong yuv_checksum = crc32(0L, Z_NULL, 0);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t deadline = now.tv_sec + 25;
    while (!output_done && now.tv_sec < deadline) {
        if (!input_done) {
            ssize_t size = AMediaExtractor_readSampleData(extractor, sample, RME_VIDEO_MAX_SAMPLE);
            uint32_t status = size < 0
                ? rme_video_submit(connection, NULL, 0, 0, VIDEO_EOS)
                : rme_video_submit(connection, sample, (uint32_t)size,
                    AMediaExtractor_getSampleTime(extractor), 0);
            if (status == VIDEO_ERROR) goto done;
            if (status == VIDEO_OK) {
                if (size < 0) input_done = 1;
                else AMediaExtractor_advance(extractor);
            }
        }
        struct video_packet packet;
        uint32_t status = rme_video_receive(connection, frame, frame_size, &packet);
        if (status == VIDEO_ERROR) goto done;
        if (status == VIDEO_OK) {
            if (packet.size > 0) {
                uint32_t frame_width, frame_height;
                if (!video_frame_dimensions(&packet, &frame_width, &frame_height) ||
                    !video_output_dimensions(&config, frame_width, frame_height)) goto done;
                yuv_checksum = crc32(yuv_checksum, frame, packet.size);
                for (int row = 0; row < height; row += 3) {
                    for (int col = 0; col < width; col += 3) {
                        checksum = crc32(checksum, frame + (size_t)row * width + col, 1);
                    }
                }
                frames++;
            }
            output_done = (packet.flags & VIDEO_EOS) != 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
    }
    if (frames && output_done) {
        printf("VIDEO_BRIDGE_OK codec=%s frames=%u lumaCRC=%08lx yuvCRC=%08lx\n",
               selected.decoder, frames, checksum, yuv_checksum);
        result = 0;
    }

done:
    if (connection >= 0) close(connection);
    if (format) AMediaFormat_delete(format);
    free(sample);
    free(frame);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: rme-video-probe PRIVATE_SOCKET CLEAR_MP4\n");
        return 2;
    }
    int fd = open(argv[2], O_RDONLY | O_CLOEXEC);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0) {
        perror("open clip");
        return 1;
    }
    AMediaExtractor *extractor = AMediaExtractor_new();
    int result = 1;
    if (extractor && AMediaExtractor_setDataSourceFd(extractor, fd, 0, info.st_size) == AMEDIA_OK) {
        result = exercise(extractor, argv[1]);
    }
    close(fd);
    if (extractor) AMediaExtractor_delete(extractor);
    if (result) fprintf(stderr, "Host hardware decode did not produce a complete stream\n");
    return result;
}
