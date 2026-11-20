/* The BLOB stream, its duration tables, and its allocation must describe one JPEG size. */
#include <stdio.h>
#include "../camera_hal3.c"

static int expected_width, expected_height, expected_capacity, expected_video;
static int seen, mismatch;
static char metadata;

static void *capture_alloc(size_t entries, size_t bytes) {
    (void)entries; (void)bytes;
    return &metadata;
}

static int capture_add(void *m, uint32_t tag, const void *data, size_t count) {
    (void)m;
    if (tag == JPEG_MAX_SIZE) {
        seen |= 1;
        mismatch |= count != 1 || *(const int32_t *)data != expected_capacity;
    } else if (tag == SCALER_AVAILABLE_STREAM_CONFIGURATIONS) {
        seen |= 2;
        const int32_t *values = data;
        mismatch |= count != (expected_video ? 32 : 28) || values[24] != FORMAT_BLOB || values[25] != expected_width ||
                    values[26] != expected_height || values[27] != 0;
        if (expected_video && count == 32)
            mismatch |= values[28] != FORMAT_IMPLEMENTATION_DEFINED || values[29] != 1920 || values[30] != 1080;
    } else if (tag == SCALER_AVAILABLE_MIN_FRAME_DURATIONS || tag == SCALER_AVAILABLE_STALL_DURATIONS) {
        seen |= tag == SCALER_AVAILABLE_MIN_FRAME_DURATIONS ? 4 : 8;
        const int64_t *values = data;
        mismatch |= count != (expected_video ? 32 : 28) || values[24] != FORMAT_BLOB || values[25] != expected_width ||
                    values[26] != expected_height;
        if (expected_video && count == 32 && tag == SCALER_AVAILABLE_MIN_FRAME_DURATIONS)
            mismatch |= values[31] != 16666666;
    } else if (tag == CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES) {
        const int32_t *values = data;
        seen |= 16;
        mismatch |= count != (expected_video ? 8 : 4) || values[count - 1] != (expected_video ? 60 : 30);
    }
    return 0;
}

static int check_camera(int id, int width, int height, int bytes, int video) {
    expected_width = width;
    expected_height = height;
    expected_capacity = bytes;
    expected_video = video;
    seen = mismatch = 0;
    if (!build_static_info(id, (struct still_size){width, height}, video) || seen != 31 || mismatch) {
        fprintf(stderr, "JPEG or video stream metadata does not match for camera %d\n", id);
        return 1;
    }
    return 0;
}

int main(void) {
    md_alloc = capture_alloc;
    md_add = capture_add;
    if (check_camera(0, 4096, 3072, 25231360, 0) ||
        check_camera(0, 4096, 3072, 25231360, 1) ||
        check_camera(1, 3264, 2448, 16046080, 0) ||
        check_camera(0, 640, 480, 1048576, 0)) return 1;
    puts("host JPEG and video stream metadata: OK");
    return 0;
}
