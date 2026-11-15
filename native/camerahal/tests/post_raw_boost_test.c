/* Exercise static_info's metadata output without an Android metadata library. */
#include <stdio.h>
#include "../camera_hal3.c"

struct metadata_capture {
    int range_count;
    int range[2];
    int listed;
};
static struct metadata_capture captures[2];
static int allocated;

static void *capture_alloc(size_t entries, size_t data) {
    (void)entries; (void)data;
    return allocated < 2 ? &captures[allocated++] : 0;
}

static int capture_add(void *metadata, uint32_t tag, const void *data, size_t count) {
    struct metadata_capture *m = metadata;
    if (tag == CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE) {
        m->range_count++;
        if (count == 2) {
            const int32_t *range = data;
            m->range[0] = range[0]; m->range[1] = range[1];
        }
    } else if (tag == REQUEST_AVAILABLE_CHARACTERISTICS_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++)
            if ((uint32_t)keys[i] == CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE) m->listed++;
    }
    return 0;
}

int main(void) {
    md_alloc = capture_alloc;
    md_add = capture_add;
    for (int id = 0; id < 2; id++) {
        const struct metadata_capture *m = static_info(id);
        if (!m || m->range_count != 1 || m->range[0] != 100 || m->range[1] != 100 || m->listed != 1) {
            fprintf(stderr, "camera %d: expected advertised post-RAW boost range [100,100]\n", id);
            return 1;
        }
    }
    puts("post-RAW boost metadata: OK");
    return 0;
}
