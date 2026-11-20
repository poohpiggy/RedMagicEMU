/* Host AVC encoder for the NX809J Guest camera using public Android Media NDK. */
#define _GNU_SOURCE
#include "camera_encode_wire.h"

#include <media/NdkMediaCodec.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

static bool is_packed(AMediaCodec *codec, const struct camera_encode_open *open) {
    AMediaFormat *input = AMediaCodec_getInputFormat(codec);
    if (!input) return false;
    int32_t stride = (int32_t)open->width;
    int32_t slice = (int32_t)open->height;
    AMediaFormat_getInt32(input, AMEDIAFORMAT_KEY_STRIDE, &stride);
    AMediaFormat_getInt32(input, "slice-height", &slice);
    AMediaFormat_delete(input);
    return stride == (int32_t)open->width && slice == (int32_t)open->height;
}

static AMediaCodec *start_encoder(const char *name, const struct camera_encode_open *open,
                                  struct camera_encode_format *reply) {
    if (strlen(name) >= sizeof reply->encoder) return NULL;
    AMediaFormat *format = AMediaFormat_new();
    if (!format) return NULL;
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, (int32_t)open->width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, (int32_t)open->height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, (int32_t)open->fps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, (int32_t)open->bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21); /* packed NV12 */
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, 1); /* BT.709 */
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, 2);    /* limited */
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 3); /* SDR video */
    AMediaCodec *codec = AMediaCodec_createCodecByName(name);
    if (codec) {
        bool started = AMediaCodec_configure(codec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                       AMediaCodec_start(codec) == AMEDIA_OK;
        if (started && is_packed(codec, open)) memcpy(reply->encoder, name, strlen(name) + 1);
        else {
            if (started) AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            codec = NULL;
        }
    }
    AMediaFormat_delete(format);
    return codec;
}

static int submit(int fd, AMediaCodec *codec, const struct video_packet *packet, uint32_t image_size, unsigned *submitted) {
    const bool eos = (packet->flags & VIDEO_EOS) != 0;
    uint32_t status = VIDEO_ERROR;
    if ((packet->flags & ~VIDEO_EOS) || packet->pts_us < 0 ||
        (packet->size != image_size && (packet->size || !eos))) return -1;
    ssize_t slot = AMediaCodec_dequeueInputBuffer(codec, 2000);
    if (slot == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        status = VIDEO_AGAIN;
        return video_write(fd, &status, sizeof status);
    }
    if (slot < 0) return -1;
    size_t capacity = 0;
    uint8_t *input = AMediaCodec_getInputBuffer(codec, (size_t)slot, &capacity);
    if (!input || capacity < packet->size) return -1;
    status = VIDEO_OK;
    if (video_write(fd, &status, sizeof status) || video_read(fd, input, packet->size)) return -1;
    if (AMediaCodec_queueInputBuffer(codec, (size_t)slot, 0, packet->size, (uint64_t)packet->pts_us,
                                    eos ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0) != AMEDIA_OK) status = VIDEO_ERROR;
    if (status == VIDEO_OK && packet->size) ++*submitted;
    return video_write(fd, &status, sizeof status) || status == VIDEO_ERROR ? -1 : 0;
}

static int receive(int fd, AMediaCodec *codec, unsigned *encoded) {
    AMediaCodecBufferInfo buffer;
    ssize_t slot = AMediaCodec_dequeueOutputBuffer(codec, &buffer, 1000);
    while (slot == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED || slot == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
        slot = AMediaCodec_dequeueOutputBuffer(codec, &buffer, 1000);
    if (slot == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        struct video_packet response = {.type = VIDEO_AGAIN};
        return video_write(fd, &response, sizeof response);
    }
    if (slot < 0) return -1;
    if (buffer.size > 0 && !(buffer.flags & VIDEO_CONFIG)) ++*encoded;
    size_t capacity = 0;
    uint8_t *output = AMediaCodec_getOutputBuffer(codec, (size_t)slot, &capacity);
    int result = -1;
    if (buffer.offset >= 0 && buffer.size >= 0 && buffer.size <= (int)RME_VIDEO_MAX_SAMPLE &&
        (!buffer.size || (output && (size_t)buffer.offset <= capacity &&
                         (size_t)buffer.size <= capacity - (size_t)buffer.offset))) {
        struct video_packet response = {.type = VIDEO_OK, .flags = buffer.flags & (VIDEO_EOS | VIDEO_SYNC | VIDEO_CONFIG),
                                        .pts_us = buffer.presentationTimeUs, .size = (uint32_t)buffer.size};
        result = video_write(fd, &response, sizeof response);
        if (!result && buffer.size) result = video_write(fd, output + buffer.offset, (size_t)buffer.size);
    }
    AMediaCodec_releaseOutputBuffer(codec, (size_t)slot, false);
    return result;
}

static void encode_client(int fd, const char *name, pid_t parent) {
    struct camera_encode_open open;
    if (video_read(fd, &open, sizeof open) || !camera_encode_dimensions(&open)) return;
    struct camera_encode_format reply = {.status = VIDEO_ERROR};
    AMediaCodec *codec = start_encoder(name, &open, &reply);
    if (codec) reply.status = VIDEO_OK;
    if (!video_write(fd, &reply, sizeof reply) && codec) {
        fprintf(stderr, "camera encoder: %s %ux%u %u fps\n", reply.encoder, open.width, open.height, open.fps);
        uint32_t image_size = open.width * open.height * 3 / 2;
        unsigned submitted = 0, encoded = 0;
        while (getppid() == parent) {
            struct pollfd event = {.fd = fd, .events = POLLIN};
            const int ready = poll(&event, 1, 1000);
            if (!ready || (ready < 0 && errno == EINTR)) continue;
            struct video_packet packet;
            if (ready < 0 || !(event.revents & POLLIN) || video_read(fd, &packet, sizeof packet)) break;
            if (packet.type == VIDEO_SUBMIT) {
                if (submit(fd, codec, &packet, image_size, &submitted)) break;
            } else if (packet.type == VIDEO_RECEIVE && !packet.size) {
                if (receive(fd, codec, &encoded)) break;
            } else break;
        }
        fprintf(stderr, "camera encoder: closed with %u submitted and %u encoded frames\n", submitted, encoded);
    }
    if (codec) {
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--probe")) {
        const struct camera_encode_open config = {
            .magic = RME_CAMERA_ENCODE_MAGIC, .width = 1920, .height = 1080, .fps = 60, .bitrate = 20000000,
        };
        struct camera_encode_format reply = {0};
        AMediaCodec *codec = start_encoder(argv[2], &config, &reply);
        if (!codec) return 1;
        printf("%s\n", reply.encoder);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        return 0;
    }
    if (argc != 5 || strcmp(argv[1], "--serve")) return 2;
    char *end;
    const long parent = strtol(argv[3], &end, 10);
    if (*end || parent <= 1 || getppid() != parent) return 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(argv[2]) >= sizeof address.sun_path) return 2;
    memcpy(address.sun_path, argv[2], strlen(argv[2]) + 1);
    umask(0077);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) return 1;
    if (bind(server, (struct sockaddr *)&address, sizeof address) || listen(server, 1)) {
        close(server);
        return 1;
    }
    while (getppid() == parent) {
        struct pollfd event = {.fd = server, .events = POLLIN};
        const int ready = poll(&event, 1, 1000);
        if (!ready || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0 || !(event.revents & POLLIN)) break;
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) { if (errno == EINTR) continue; break; }
        struct timeval timeout = {.tv_sec = 3};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        struct ucred peer;
        socklen_t length = sizeof peer;
        if (!getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) && peer.uid == getuid())
            encode_client(client, argv[4], (pid_t)parent);
        close(client);
    }
    close(server);
    unlink(address.sun_path);
    return 1;
}
