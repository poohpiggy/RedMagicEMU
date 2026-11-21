/* The camera app requires an exposure time in each successful capture result. */
#include <stdio.h>
#include "../camera_hal3.c"

struct capture {
    int exposure_entries, iso_entries, boost_entries, listed;
    int64_t exposure;
    int32_t iso, boost;
    float gains[4];
};
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
    if (tag == COLOR_CORRECTION_GAINS && count == 4) memcpy(c->gains, data, sizeof(c->gains));
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
static int capture_append(void *m, const void *s) {
    (void)s;
    const int64_t requested_exposure = 12500000;
    const int32_t requested_iso = 800;
    capture_add(m, SENSOR_EXPOSURE_TIME, &requested_exposure, 1);
    capture_add(m, SENSOR_SENSITIVITY, &requested_iso, 1);
    return 0;
}
static int capture_find(const void *m, uint32_t tag, struct ro_entry *entry) {
    const struct capture *c = m;
    if (tag == SENSOR_EXPOSURE_TIME && c->exposure_entries)
        *entry = (struct ro_entry){.index = 0, .count = 1, .data = &c->exposure};
    else if (tag == SENSOR_SENSITIVITY && c->iso_entries)
        *entry = (struct ro_entry){.index = 1, .count = 1, .data = &c->iso};
    else return -1;
    return 0;
}
static int capture_update(void *m, size_t index, const void *data, size_t count, void *entry) {
    (void)entry;
    struct capture *c = m;
    if (count != 1) return -1;
    if (index == 0) c->exposure = *(const int64_t *)data;
    else if (index == 1) c->iso = *(const int32_t *)data;
    else return -1;
    return 0;
}

int main(void) {
    md_alloc = capture_alloc; md_add = capture_add;
    md_entries = capture_size; md_data = capture_size; md_append = capture_append;
    md_find = capture_find; md_update = capture_update;
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
    d.result = (struct pro_result){
        .exposure = 12500000, .frame_duration = 33333333, .iso = 800,
        .gains = {1.5f, 1.0f, 1.0f, 2.25f},
    };
    const struct capture *r = result_metadata(&d, 123456789);
    if (!r || r->exposure_entries != 1 || r->iso_entries != 1 || r->iso != 800 ||
        r->exposure != 12500000 || memcmp(r->gains, d.result.gains, sizeof(r->gains))) {
        fprintf(stderr, "Pro result did not report the physical sensor exposure and gains\n");
        return 1;
    }
    puts("virtual fallback and measured Pro sensor result: OK");
    return 0;
}
