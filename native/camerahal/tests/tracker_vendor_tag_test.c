/* The optional tracking result name must resolve without reporting tracking. */
#include <stdio.h>
#define CAM_VENDOR_TAGS "camera_tags_29.h"
#include "../camera_hal3.c"

static uint32_t tracker_tag;
struct metadata_capture { int values, advertised; };
static struct metadata_capture capture;

static void *capture_alloc(size_t entries, size_t data) {
    (void)entries; (void)data;
    return &capture;
}
static int capture_add(void *metadata, uint32_t tag, const void *data, size_t count) {
    struct metadata_capture *m = metadata;
    if (tag == tracker_tag) m->values++;
    if (tag == REQUEST_AVAILABLE_RESULT_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++)
            if ((uint32_t)keys[i] == tracker_tag) m->advertised++;
    }
    return 0;
}

int main(void) {
    struct vendor_tag_ops ops = {0};
    mod_vendor_tag_ops(&ops);
    tracker_tag = vendor_tag("org.quic.camera2.objectTrackingResults", "TrackerStatus");
    if (tracker_tag != VTAG0 + 290 || ops.get_tag_type(&ops, tracker_tag) != 1 ||
        !ops.get_tag_name(&ops, tracker_tag) || !ops.get_section_name(&ops, tracker_tag)) {
        fprintf(stderr, "optional tracker key is not registered as an appended int32 vendor tag\n");
        return 1;
    }
    md_alloc = capture_alloc;
    md_add = capture_add;
    for (int id = 0; id < 2; id++) {
        if (!static_info(id) || capture.values || capture.advertised) {
            fprintf(stderr, "camera %d unexpectedly advertises a tracker result\n", id);
            return 1;
        }
        struct camera3 d = {0};
        if (!result_metadata(&d, 123) || capture.values) {
            fprintf(stderr, "camera %d unexpectedly reports a tracker result\n", id);
            return 1;
        }
    }
    puts("optional tracker vendor tag: OK");
    return 0;
}
