/* Register the optional Qualcomm ISO tag without advertising a value. */
#include <stdio.h>
#define CAM_VENDOR_TAGS "camera_tags_29.h"
#include "../camera_hal3.c"

static uint32_t iso_tag;
struct metadata_capture { int value_count, key_count; };
static struct metadata_capture captures[2];
static int allocated;

static void *capture_alloc(size_t entries, size_t data) {
    (void)entries; (void)data;
    return allocated < 2 ? &captures[allocated++] : 0;
}
static int capture_add(void *metadata, uint32_t tag, const void *data, size_t count) {
    struct metadata_capture *m = metadata;
    if (tag == iso_tag) m->value_count++;
    if (tag == REQUEST_AVAILABLE_CHARACTERISTICS_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++)
            if ((uint32_t)keys[i] == iso_tag) m->key_count++;
    }
    return 0;
}

int main(void) {
    struct vendor_tag_ops ops = {0};
    mod_vendor_tag_ops(&ops);
    iso_tag = vendor_tag("org.codeaurora.qcamera3.iso_exp_priority", "iso_available_modes");
    if (iso_tag != VTAG0 + 289 || ops.get_tag_type(&ops, iso_tag) != 1 ||
        !ops.get_tag_name(&ops, iso_tag) || !ops.get_section_name(&ops, iso_tag)) {
        fprintf(stderr, "optional ISO key is not registered as an appended int32 vendor tag\n");
        return 1;
    }
    md_alloc = capture_alloc;
    md_add = capture_add;
    for (int id = 0; id < 2; id++) {
        const struct metadata_capture *m = static_info(id);
        if (!m || m->value_count || m->key_count) {
            fprintf(stderr, "camera %d unexpectedly advertises ISO modes\n", id);
            return 1;
        }
    }
    puts("optional ISO vendor tag: OK");
    return 0;
}
