/* Resolve the optional Qualcomm lux result without inventing a host measurement. */
#include <stdio.h>
#define CAM_VENDOR_TAGS "camera_tags_29.h"
#include "../camera_hal3.c"

static uint32_t lux_tag;
struct metadata_capture { int values, advertised; };
static struct metadata_capture capture;

static void *capture_alloc(size_t entries, size_t data) {
    (void)entries; (void)data;
    return &capture;
}
static int capture_add(void *metadata, uint32_t tag, const void *data, size_t count) {
    struct metadata_capture *m = metadata;
    if (tag == lux_tag) m->values++;
    if (tag == REQUEST_AVAILABLE_RESULT_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++) if ((uint32_t)keys[i] == lux_tag) m->advertised++;
    }
    return 0;
}

int main(void) {
    struct vendor_tag_ops ops = {0};
    mod_vendor_tag_ops(&ops);
    lux_tag = vendor_tag("org.quic.camera2.statsconfigs", "AECLuxIndex");
    if (lux_tag != VTAG0 + 291 || ops.get_tag_type(&ops, lux_tag) != 2 ||
        !ops.get_tag_name(&ops, lux_tag) || !ops.get_section_name(&ops, lux_tag)) {
        fprintf(stderr, "optional lux key is not registered as an appended float vendor tag\n");
        return 1;
    }
    md_alloc = capture_alloc; md_add = capture_add;
    for (int id = 0; id < 2; id++) {
        capture = (struct metadata_capture){0};
        if (!static_info(id) || capture.values || capture.advertised) {
            fprintf(stderr, "camera %d unexpectedly advertises a lux value\n", id); return 1;
        }
        struct camera3 d = {0};
        if (!result_metadata(&d, 123) || capture.values) {
            fprintf(stderr, "camera %d unexpectedly reports a lux value\n", id); return 1;
        }
    }
    puts("optional lux vendor tag: OK");
    return 0;
}
