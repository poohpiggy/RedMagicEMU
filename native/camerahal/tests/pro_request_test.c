/* The stock Pro app sends a three-channel tint through a vendor request key. */
#define CAM_VENDOR_TAGS "camera_tags_29.h"
#include "../camera_hal3.c"

static uint8_t template_af;

static void *record_template_alloc(size_t entries, size_t bytes) {
    (void)entries; (void)bytes;
    return &template_af;
}

static int record_template_add(void *metadata, uint32_t tag, const void *value, size_t count) {
    (void)metadata;
    if (tag == CONTROL_AF_MODE && count == 1) template_af = *(const uint8_t *)value;
    return 0;
}

static int request_find(const void *m, uint32_t tag, struct ro_entry *entry) {
    (void)m;
    static const uint8_t awb = 0, af = 0;
    static const float gains[] = {1.5f, 1.25f, 2.0f}, focus = 4.0f;
    static const int32_t tint = 2, color_tint = 47, ev = -6, priority = 1, iso = 800;
    static const int64_t shutter = 125000000;
    const char *ae_section = "org.codeaurora.qcamera3.iso_exp_priority";
    const char *wb_section = "org.codeaurora.qcamera3.manualWB";
    *entry = (struct ro_entry){.count = 1};
    if (tag == CONTROL_AWB_MODE) entry->data = &awb;
    else if (tag == CONTROL_AF_MODE) entry->data = &af;
    else if (tag == CONTROL_AE_EXPOSURE_COMPENSATION) entry->data = &ev;
    else if (tag == LENS_FOCUS_DISTANCE) entry->data = &focus;
    else if (tag == vendor_tag(ae_section, "select_priority")) entry->data = &priority;
    else if (tag == vendor_tag(ae_section, "use_iso_exp_priority")) entry->data = &shutter;
    else if (tag == vendor_tag(ae_section, "use_iso_value")) entry->data = &iso;
    else if (tag && tag == vendor_tag("com.nubia.chi", "color_tint")) entry->data = &color_tint;
    else if (tag && tag == vendor_tag(wb_section, "partial_mwb_mode")) entry->data = &tint;
    else if (tag && tag == vendor_tag(wb_section, "gains")) {
        entry->count = 3;
        entry->data = gains;
    } else return -1;
    return 0;
}

int main(void) {
    struct camera3 camera = {.fd = -1, .pro_mode = 1};
    camera.last = &camera;
    md_find = request_find;
    const char expected[] = "controls -6 800 125000000 0 4 0 1.5 1.25 1.25 2\n";
    if (send_pro_controls(&camera) != 125000000 || strcmp(camera.pro_command, expected)) {
        fprintf(stderr, "stock Pro did not forward ISO, shutter, EV, focus and tint: %s", camera.pro_command);
        return 1;
    }
    md_alloc = record_template_alloc;
    md_add = record_template_add;
    if (!d_template(&camera, 3) || template_af != 4) {
        fprintf(stderr, "the rear record template must default to advertised automatic focus\n");
        return 1;
    }
    for (unsigned mode = 0x8004; mode <= 0x800c; mode += 8) {
        struct stream encoder = {
            .width = 1920, .height = 1080, .format = FORMAT_IMPLEMENTATION_DEFINED, .usage = USAGE_VIDEO_ENCODER,
        };
        struct stream *streams[] = {&encoder};
        struct stream_config config = {.num = 1, .streams = streams, .mode = mode};
        struct camera3 video = {.fd = -1, .video_enabled = 1};
        video.last = &video;
        if (d_configure(&video, &config) || !video.pro_mode || send_pro_controls(&video) != 125000000 ||
            strcmp(video.pro_command, "video-controls -6 800 125000000 0 4 0 1.5 1.25 1.25 2 47\n")) {
            fprintf(stderr, "rear stock video mode %x did not forward the manual controls: %s", mode, video.pro_command);
            return 1;
        }
    }
    puts("stock Pro control forwarding: OK");
    return 0;
}
