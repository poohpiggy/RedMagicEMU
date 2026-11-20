/* The BLOB stream, its duration tables, and its allocation must describe one JPEG size. */
#include <stdio.h>
#include "../camera_hal3.c"

static int expected_width, expected_height, expected_capacity;
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
        mismatch |= count != 28 || values[24] != FORMAT_BLOB || values[25] != expected_width ||
                    values[26] != expected_height || values[27] != 0;
    } else if (tag == SCALER_AVAILABLE_MIN_FRAME_DURATIONS || tag == SCALER_AVAILABLE_STALL_DURATIONS) {
        seen |= tag == SCALER_AVAILABLE_MIN_FRAME_DURATIONS ? 4 : 8;
        const int64_t *values = data;
        mismatch |= count != 28 || values[24] != FORMAT_BLOB || values[25] != expected_width ||
                    values[26] != expected_height;
    }
    return 0;
}

static int check_jpeg(int id, int width, int height, int bytes) {
    expected_width = width;
    expected_height = height;
    expected_capacity = bytes;
    seen = mismatch = 0;
    if (!build_static_info(id, (struct still_size){width, height}) || seen != 15 || mismatch) {
        fprintf(stderr, "JPEG dimensions or allocation do not match for camera %d\n", id);
        return 1;
    }
    return 0;
}

int main(void) {
    md_alloc = capture_alloc;
    md_add = capture_add;
    if (check_jpeg(0, 4096, 3072, 25231360) ||
        check_jpeg(1, 3264, 2448, 16046080) ||
        check_jpeg(0, 640, 480, 1048576)) return 1;
    puts("host JPEG stream metadata: OK");
    return 0;
}
