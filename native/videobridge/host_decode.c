/*
 * Host-side hardware video decoder, using the public Android NDK.
 * One private local connection carries one clear, 8-bit YUV420 decoder session.
 */
#define _GNU_SOURCE
#include "video_wire.h"

#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaCodecStore.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

struct decoder {
    AMediaCodec *codec;
    AImageReader *reader;
    struct video_open params;
    uint8_t *frame;
    uint8_t *sample;
    size_t sample_capacity;
    bool running;
};

static media_status_t (*next_decoder)(const AMediaFormat *, const AMediaCodecInfo **);
static AMediaCodecType (*decoder_type)(const AMediaCodecInfo *);
static const char *(*decoder_name)(const AMediaCodecInfo *);

static bool has_hardware_decoder(uint32_t codec) {
    AMediaFormat *format = AMediaFormat_new();
    if (!format) return false;
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, video_mime(codec));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 640);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 360);
    const AMediaCodecInfo *entry = NULL;
    bool hardware = false;
    while (next_decoder(format, &entry) == AMEDIA_OK && entry) {
        if (decoder_type(entry) == AMediaCodecType_HARDWARE_ACCELERATED) {
            hardware = true;
            break;
        }
    }
    AMediaFormat_delete(format);
    return hardware;
}

static void release_decoder(struct decoder *decoder) {
    if (decoder->codec) {
        if (decoder->running) AMediaCodec_stop(decoder->codec);
        AMediaCodec_delete(decoder->codec);
    }
    if (decoder->reader) AImageReader_delete(decoder->reader);
    free(decoder->frame);
    free(decoder->sample);
}

static size_t frame_size(const struct decoder *decoder) {
    return (size_t)decoder->params.width * decoder->params.height * 3 / 2;
}

static int start_decoder(int fd, struct decoder *decoder, struct video_format *reply) {
    struct video_open *open = &decoder->params;
    if (video_read(fd, open, sizeof *open) != 0 || open->magic != RME_VIDEO_MAGIC ||
        !video_dimensions(open->width, open->height) || !video_mime(open->codec) ||
        open->csd0_size > RME_VIDEO_MAX_CONFIG || open->csd1_size > RME_VIDEO_MAX_CONFIG) {
        return -1;
    }

    AMediaFormat *format = AMediaFormat_new();
    if (!format) return -1;
    const char *mime = video_mime(open->codec);
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, (int32_t)open->width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, (int32_t)open->height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7f420888); /* Flexible YUV420 */
    uint8_t *csd = malloc(RME_VIDEO_MAX_CONFIG);
    if (!csd) {
        AMediaFormat_delete(format);
        return -1;
    }
    const uint32_t config_sizes[] = {open->csd0_size, open->csd1_size};
    const char *const config_keys[] = {"csd-0", "csd-1"};
    int status = -1;
    for (size_t slot = 0; slot < 2; ++slot) {
        if (video_read(fd, csd, config_sizes[slot]) != 0) goto finish;
        if (config_sizes[slot]) AMediaFormat_setBuffer(format, config_keys[slot], csd, config_sizes[slot]);
    }

    ANativeWindow *window = NULL;
    if (AImageReader_newWithUsage((int32_t)open->width, (int32_t)open->height,
            AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 2, &decoder->reader) != AMEDIA_OK ||
        AImageReader_getWindow(decoder->reader, &window) != AMEDIA_OK) {
        goto finish;
    }
    decoder->frame = malloc(frame_size(decoder));
    if (!decoder->frame) goto finish;

    const AMediaCodecInfo *entry = NULL;
    while (next_decoder(format, &entry) == AMEDIA_OK && entry) {
        if (decoder_type(entry) != AMediaCodecType_HARDWARE_ACCELERATED) continue;
        const char *name = decoder_name(entry);
        if (!name || strlen(name) >= sizeof reply->decoder) continue;
        decoder->codec = AMediaCodec_createCodecByName(name);
        if (!decoder->codec) continue;
        if (AMediaCodec_configure(decoder->codec, format, window, NULL, 0) == AMEDIA_OK &&
            AMediaCodec_start(decoder->codec) == AMEDIA_OK) {
            decoder->running = true;
            memcpy(reply->decoder, name, strlen(name) + 1);
            status = 0;
            break;
        }
        AMediaCodec_delete(decoder->codec);
        decoder->codec = NULL;
    }

finish:
    free(csd);
    AMediaFormat_delete(format);
    return status;
}

static uint32_t submit(int fd, struct decoder *decoder, const struct video_packet *packet) {
    if (packet->size > RME_VIDEO_MAX_SAMPLE ||
        (packet->flags & ~(uint32_t)(VIDEO_SYNC | VIDEO_CONFIG | VIDEO_EOS))) return VIDEO_ERROR;
    if (decoder->sample_capacity < packet->size) {
        uint8_t *buffer = realloc(decoder->sample, packet->size);
        if (!buffer) return VIDEO_ERROR;
        decoder->sample = buffer;
        decoder->sample_capacity = packet->size;
    }
    if (video_read(fd, decoder->sample, packet->size) != 0) return VIDEO_ERROR;
    ssize_t slot = AMediaCodec_dequeueInputBuffer(decoder->codec, 5000);
    if (slot == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return VIDEO_AGAIN;
    if (slot < 0) return VIDEO_ERROR;
    size_t capacity;
    uint8_t *input = AMediaCodec_getInputBuffer(decoder->codec, (size_t)slot, &capacity);
    if (!input || capacity < packet->size) return VIDEO_ERROR;
    if (packet->size) memcpy(input, decoder->sample, packet->size);
    media_status_t result = AMediaCodec_queueInputBuffer(decoder->codec, (size_t)slot, 0,
            packet->size, (uint64_t)packet->pts_us, packet->flags);
    return result == AMEDIA_OK ? VIDEO_OK : VIDEO_ERROR;
}

static int copy_frame(struct decoder *decoder, AImage *image) {
    AImageCropRect crop = {0};
    if (AImage_getCropRect(image, &crop) != AMEDIA_OK || crop.left < 0 || crop.top < 0 ||
        crop.right - crop.left != (int32_t)decoder->params.width ||
        crop.bottom - crop.top != (int32_t)decoder->params.height) {
        fprintf(stderr, "video: unexpected crop (%d,%d)-(%d,%d)\n", crop.left, crop.top, crop.right, crop.bottom);
        return -1;
    }
    uint8_t *out = decoder->frame;
    for (int plane = 0; plane < 3; ++plane) {
        int shift = plane != 0;
        int width = (int)decoder->params.width >> shift;
        int height = (int)decoder->params.height >> shift;
        int32_t row_stride = 0, pixel_stride = 0, size = 0;
        uint8_t *data = NULL;
        media_status_t mapped = AImage_getPlaneData(image, plane, &data, &size);
        media_status_t row_status = AImage_getPlaneRowStride(image, plane, &row_stride);
        media_status_t pixel_status = AImage_getPlanePixelStride(image, plane, &pixel_stride);
        if (mapped != AMEDIA_OK || !data || size <= 0 ||
            row_status != AMEDIA_OK || row_stride <= 0 || pixel_status != AMEDIA_OK || pixel_stride <= 0) {
            fprintf(stderr, "video: plane %d data=%d bytes=%d row=%d(%d) pixel=%d(%d)\n",
                    plane, mapped, size, row_stride, row_status, pixel_stride, pixel_status);
            return -1;
        }
        int64_t start = (int64_t)(crop.top >> shift) * row_stride + (int64_t)(crop.left >> shift) * pixel_stride;
        int64_t end = start + (int64_t)(height - 1) * row_stride + (int64_t)(width - 1) * pixel_stride;
        if (start < 0 || end >= size) {
            fprintf(stderr, "video: plane %d has %d bytes, needs %lld\n", plane, size, (long long)end + 1);
            return -1;
        }
        for (int row = 0; row < height; ++row) {
            const uint8_t *src = data + start + (int64_t)row * row_stride;
            if (pixel_stride == 1) memcpy(out, src, (size_t)width);
            else for (int col = 0; col < width; ++col) out[col] = src[col * pixel_stride];
            out += width;
        }
    }
    return 0;
}

static int acquire_frame(struct decoder *decoder) {
    const struct timespec delay = { .tv_nsec = 1000000 };
    for (unsigned attempt = 0; attempt < 500; ++attempt) {
        AImage *image = NULL;
        media_status_t result = AImageReader_acquireNextImage(decoder->reader, &image);
        if (result == AMEDIA_OK) {
            int copied = copy_frame(decoder, image);
            AImage_delete(image);
            return copied;
        }
        if (result != AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
            fprintf(stderr, "video: image acquisition failed (%d)\n", result);
            return -1;
        }
        nanosleep(&delay, NULL);
    }
    fprintf(stderr, "video: timed out waiting for the output image\n");
    return -1;
}

static uint32_t receive(struct decoder *decoder, struct video_packet *reply) {
    AMediaCodecBufferInfo output;
    ssize_t slot = AMediaCodec_dequeueOutputBuffer(decoder->codec, &output, 5000);
    if (slot == AMEDIACODEC_INFO_TRY_AGAIN_LATER || slot == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
        slot == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) return VIDEO_AGAIN;
    if (slot < 0) return VIDEO_ERROR;
    reply->pts_us = output.presentationTimeUs;
    reply->flags = output.flags & (VIDEO_SYNC | VIDEO_EOS);
    bool has_frame = output.size > 0 && !(output.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
    if (AMediaCodec_releaseOutputBuffer(decoder->codec, (size_t)slot, has_frame) != AMEDIA_OK) return VIDEO_ERROR;
    if (has_frame) {
        if (acquire_frame(decoder) != 0) return VIDEO_ERROR;
        reply->size = (uint32_t)frame_size(decoder);
    }
    return VIDEO_OK;
}

static void session(int fd, pid_t parent) {
    struct decoder decoder = {0};
    struct video_format format = {0};
    format.status = start_decoder(fd, &decoder, &format) == 0 ? VIDEO_OK : VIDEO_ERROR;
    if (video_write(fd, &format, sizeof format) != 0 || format.status != VIDEO_OK) goto close;
    fprintf(stderr, "video: using %s %ux%u\n", format.decoder, decoder.params.width, decoder.params.height);

    struct video_packet request;
    while ((!parent || getppid() == parent) && video_read(fd, &request, sizeof request) == 0) {
        struct video_packet reply = {.type = VIDEO_ERROR};
        if (request.reserved == 0 && request.pts_us >= 0) {
            if (request.type == VIDEO_SUBMIT) reply.type = submit(fd, &decoder, &request);
            else if (!request.size && !request.flags) {
                if (request.type == VIDEO_RECEIVE) reply.type = receive(&decoder, &reply);
                if (request.type == VIDEO_FLUSH) {
                    reply.type = AMediaCodec_flush(decoder.codec) == AMEDIA_OK ? VIDEO_OK : VIDEO_ERROR;
                }
            }
        }
        if (video_write(fd, &reply, sizeof reply) != 0 || reply.type == VIDEO_ERROR ||
            video_write(fd, decoder.frame, reply.size) != 0) break;
    }

close:
    release_decoder(&decoder);
}

int main(int argc, char **argv) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if ((argc != 2 && argc != 3) || strlen(argv[1]) >= sizeof address.sun_path) {
        fprintf(stderr, "usage: rme-video-host PATH_TO_NEW_PRIVATE_SOCKET [PARENT_PID]\n");
        return 2;
    }
    pid_t parent = 0;
    if (argc == 3) {
        char *end;
        long expected = strtol(argv[2], &end, 10);
        if (*end || expected <= 1 || getppid() != expected) {
            fprintf(stderr, "video: VM parent is unavailable\n");
            return 1;
        }
        parent = (pid_t)expected;
    }
    void *media = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
    if (!media) {
        fprintf(stderr, "video: could not load Android Media NDK\n");
        return 1;
    }
    next_decoder = dlsym(media, "AMediaCodecStore_findNextDecoderForFormat");
    decoder_type = dlsym(media, "AMediaCodecInfo_getMediaCodecInfoType");
    decoder_name = dlsym(media, "AMediaCodecInfo_getCanonicalName");
    if (!next_decoder || !decoder_type || !decoder_name) {
        fprintf(stderr, "video: Android Media NDK hardware codec discovery is unavailable\n");
        return 1;
    }
    if (!has_hardware_decoder(VIDEO_AVC) || !has_hardware_decoder(VIDEO_HEVC)) {
        fprintf(stderr, "video: host AVC and HEVC hardware decoders are unavailable\n");
        return 1;
    }
    strcpy(address.sun_path, argv[1]);
    umask(077);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof address) != 0) {
        perror("video bind");
        return 1;
    }
    if (listen(server, 1) != 0) {
        perror("video listen");
        unlink(address.sun_path);
        return 1;
    }
    while (!parent || getppid() == parent) {
        struct pollfd event = { .fd = server, .events = POLLIN };
        int ready = poll(&event, 1, 1000);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0 || !(event.revents & POLLIN)) break;
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0 && errno == EINTR) continue;
        if (client < 0) break;
        struct ucred peer;
        socklen_t length = sizeof peer;
        struct timeval timeout = {.tv_sec = 10};
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0 && peer.uid == getuid() &&
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0 &&
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == 0) {
            session(client, parent);
        }
        close(client);
    }
    close(server);
    unlink(address.sun_path);
    return 1;
}
