#include <assert.h>
#include <stdio.h>
static FILE *profile_file(const char *path, const char *mode);
#define fopen profile_file
#include "../camera_hal3.c"
#undef fopen

static int supports_continuous_focus;
static const char *profile_name = "/dev/.aemu-camera-controls";
static FILE *profile_file(const char *path, const char *mode) {
    (void)mode;
    assert(strcmp(path, profile_name) == 0);
    FILE *file = tmpfile();
    assert(file);
    struct pro_caps caps[2] = {0};
    if (supports_continuous_focus) caps[0].af_modes = 1u << 4;
    assert(fwrite(caps, sizeof caps, 1, file) == 1);
    rewind(file);
    return file;
}

static uint8_t af_mode;
static void *allocate_metadata(size_t entries, size_t bytes) {
    (void)entries;
    (void)bytes;
    return &af_mode;
}
static int capture_metadata(void *metadata, uint32_t tag, const void *value, size_t count) {
    (void)metadata;
    if (tag == CONTROL_AF_MODE) {
        assert(count == 1);
        af_mode = *(const uint8_t *)value;
    }
    return 0;
}
static void check_photo_focus(int capabilities, int type, int expected) {
    struct camera3 device = {0};
    supports_continuous_focus = capabilities;
    af_mode = 255;
    md_alloc = allocate_metadata;
    md_add = capture_metadata;
    assert(d_template(&device, type));
    assert(af_mode == expected);
}

static void read_preview(int width, int height, int accepted) {
    const int bytes = width * height * 11 / 2;
    const int32_t header[] = {1, width, height, bytes};
    FILE *input = tmpfile();
    assert(input);
    unsigned char *pixels = calloc(1, bytes);
    assert(pixels);
    pixels[bytes - 1] = 89;
    assert(fwrite(header, sizeof header, 1, input) == 1);
    assert(fwrite(pixels, bytes, 1, input) == 1);
    rewind(input);
    struct camera3 device = { .fd = fileno(input), .running = 1 };
    reader(&device);
    assert(device.frame_seq == (unsigned)accepted);
    if (accepted) {
        assert(device.frame_width == width && device.frame_height == height);
        assert(device.frame[bytes - 1] == 89);
    }
    free(device.frame);
    free(pixels);
    fclose(input);
}

int main(void) {
    read_preview(640, 480, 1);
    read_preview(1280, 960, 1);
    read_preview(1280, 720, 0);
    read_preview(1440, 1080, 1);
    read_preview(1600, 1200, 0);
    check_photo_focus(1, 1, 4);
    check_photo_focus(1, 2, 4);
    check_photo_focus(0, 1, 0);
    check_photo_focus(1, 6, 0);
    return 0;
}
