/* The camera app requires an exposure time in each successful capture result. */
#include <stdio.h>
#include "../camera_hal3.c"

struct capture { int exposure_entries, iso_entries, boost_entries, listed; int64_t exposure; int32_t iso, boost; };
static struct capture metadata;
static void *capture_alloc(size_t entries, size_t data) {
    (void)entries; (void)data;
    metadata = (struct capture){0};
    return &metadata;
}
static int capture_add(void *m, uint32_t tag, const void *data, size_t count) {
    struct capture *c = m;
    if (tag == SENSOR_EXPOSURE_TIME) {
        c->exposure_entries++;
        if (count == 1) c->exposure = *(const int64_t *)data;
    }
    if (tag == SENSOR_SENSITIVITY) { c->iso_entries++; if (count == 1) c->iso = *(const int32_t *)data; }
    if (tag == CONTROL_POST_RAW_SENSITIVITY_BOOST) { c->boost_entries++; if (count == 1) c->boost = *(const int32_t *)data; }
    if (tag == REQUEST_AVAILABLE_RESULT_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++) {
            if ((uint32_t)keys[i] == SENSOR_EXPOSURE_TIME) c->listed |= 1;
            if ((uint32_t)keys[i] == SENSOR_SENSITIVITY) c->listed |= 2;
            if ((uint32_t)keys[i] == CONTROL_POST_RAW_SENSITIVITY_BOOST) c->listed |= 4;
        }
    }
    return 0;
}
static size_t capture_size(const void *m) { (void)m; return 0; }
static int capture_append(void *m, const void *s) { (void)m; (void)s; return 0; }

int main(void) {
    md_alloc = capture_alloc; md_add = capture_add;
    md_entries = capture_size; md_data = capture_size; md_append = capture_append;
    const struct capture *s = static_info(0);
    if (!s || s->listed != 7) { fprintf(stderr, "nominal exposure result keys not all advertised\n"); return 1; }
    struct camera3 d = {0};
    for (int i = 0; i < 2; i++) {
        d.last = i ? &d : 0; /* both an initial result and one with request metadata */
        const struct capture *r = result_metadata(&d, 123456789);
        if (!r || r->exposure_entries != 1 || r->iso_entries != 1 || r->boost_entries != 1 ||
            r->exposure != 33333333 || r->iso != 100 || r->boost != 100) {
            fprintf(stderr, "capture result missing nominal exposure, ISO or post-RAW boost\n"); return 1;
        }
    }
    puts("nominal virtual exposure result: OK");
    return 0;
}
