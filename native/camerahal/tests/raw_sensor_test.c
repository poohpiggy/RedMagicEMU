#include "../camera_hal3.c"
extern int pipe(int fds[2]);

struct observed {
    unsigned formats, capability, active_array, cfa;
    int32_t crop[4];
};
static struct observed got;

static void *capture_alloc(size_t entries, size_t bytes) {
    (void)entries; (void)bytes;
    got = (struct observed){0};
    return &got;
}

static int capture_add(void *metadata, uint32_t tag, const void *value, size_t count) {
    (void)metadata;
    if (tag == SCALER_AVAILABLE_STREAM_CONFIGURATIONS && count % 4 == 0) {
        const int32_t *streams = value;
        for (size_t i = 0; i < count; i += 4) {
            if (streams[i] == FORMAT_RAW_SENSOR && streams[i + 1] == 4096 && streams[i + 2] == 3072 && streams[i + 3] == 0)
                got.formats++;
        }
    } else if (tag == REQUEST_AVAILABLE_CAPABILITIES) {
        const uint8_t *caps = value;
        for (size_t i = 0; i < count; ++i) if (caps[i] == 3) got.capability++;
    } else if (tag == SENSOR_INFO_ACTIVE_ARRAY_SIZE && count == 4) {
        const int32_t *active = value;
        got.active_array += active[0] == 8 && active[1] == 8 && active[2] == 4080 && active[3] == 3056;
    } else if (tag == SENSOR_INFO_COLOR_FILTER_ARRANGEMENT && count == 1) got.cfa += *(const uint8_t *)value == 3;
    else if (tag == SCALER_CROP_REGION && count == 4) memcpy(got.crop, value, sizeof(got.crop));
    return 0;
}

static int raw_pair_accepted(unsigned pending, uint32_t request) {
    int sockets[2];
    if (pipe(sockets) != 0) return -1;
    struct camera3 camera = {
        .fd = sockets[0], .running = 1, .raw_pending = pending, .raw_request = 14,
        .raw = {.width = 16, .height = 2}, .still_size = {16, 2},
    };
    const unsigned char samples[64] = {42};
    const unsigned char jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
    const struct raw_result raw = {.timestamp = 123};
    const struct pro_result controls = {.iso = 200, .exposure = 10000000};
    const int32_t header[] = {8, 16, 2, sizeof(request) + sizeof(raw) + sizeof(controls) + sizeof(samples) + sizeof(jpeg)};
    int written = write(sockets[1], header, sizeof(header)) == sizeof(header) &&
                  write(sockets[1], &request, sizeof(request)) == sizeof(request) &&
                  write(sockets[1], &raw, sizeof(raw)) == sizeof(raw) &&
                  write(sockets[1], &controls, sizeof(controls)) == sizeof(controls) &&
                  write(sockets[1], samples, sizeof(samples)) == sizeof(samples) &&
                  write(sockets[1], jpeg, sizeof(jpeg)) == sizeof(jpeg);
    close(sockets[1]);
    reader(&camera);
    close(sockets[0]);
    int accepted = camera.raw_frame && camera.jpeg && camera.jpeg_seq == 1 &&
                   camera.raw_frame[0] == samples[0] && camera.raw_metadata.timestamp == raw.timestamp &&
                   camera.jpeg_len == sizeof(jpeg) && !memcmp(camera.jpeg, jpeg, sizeof(jpeg));
    int complete = accepted || (!camera.raw_frame && !camera.jpeg && camera.jpeg_seq == 0);
    free(camera.raw_frame);
    free(camera.jpeg);
    return written && complete ? accepted : -1;
}

int main(void) {
    const struct raw_profile profile = {
        .width = 4096, .height = 3072, .min_duration = 33333333, .stall_duration = 50000000,
        .pixels = {4096, 3072}, .active = {8, 8, 4080, 3056}, .precorrection = {8, 8, 4080, 3056},
        .white = 1023, .black = {64, 64, 64, 64}, .analog_iso = 1200, .cfa = 3,
        .illuminant1 = 21, .aperture = 1.6f, .focal = 5.84f, .version = 1,
    };
    md_alloc = capture_alloc; md_add = capture_add;
    if (!build_static_info_with_raw(0, (struct still_size){4096, 3072}, 1, &profile) ||
        got.formats != 1 || got.capability != 1 || got.active_array != 1 || got.cfa != 1) {
        fprintf(stderr, "RAW16 stream or physical sensor characteristics missing\n");
        return 1;
    }
    if (!build_static_info_with_raw(0, (struct still_size){4096, 3072}, 0, NULL) ||
        got.formats || got.capability || got.active_array || got.cfa) {
        fprintf(stderr, "RAW must be unavailable without a physical calibration profile\n");
        return 1;
    }
    struct camera3 camera = {.raw = profile};
    if (!d_template(&camera, 3) || memcmp(got.crop, profile.active, sizeof(got.crop))) {
        fprintf(stderr, "RAW rear default video crop must span the full active array\n");
        return 1;
    }
    if (raw_pair_accepted(1, 14) != 1 || raw_pair_accepted(1, 13) != 0 || raw_pair_accepted(0, 14) != 0) {
        fprintf(stderr, "RAW and JPEG must arrive together for the pending shot only\n");
        return 1;
    }
    return 0;
}
