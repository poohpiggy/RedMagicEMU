/* Exercise the HAL's exported tag callbacks against Android's name lookup. */
#include <stdio.h>
#include <string.h>
#define CAM_VENDOR_TAGS "camera_tags_29.h"
#include "../camera_hal3.c"

int main(void) {
    struct vendor_tag_ops ops = {0};
    uint32_t tags[NVTAGS];
    mod_vendor_tag_ops(&ops);
    int n = ops.get_tag_count(&ops);
    if (n < 0 || n > NVTAGS) return 1;
    ops.get_all_tags(&ops, tags);
    for (int i = 0; i < n; i++) {
        const char *section = ops.get_section_name(&ops, tags[i]);
        const char *name = ops.get_tag_name(&ops, tags[i]);
        char full[512];
        if (!section || !name || snprintf(full, sizeof(full), "%s.%s", section, name) >= sizeof(full))
            return 1;
        size_t longest = 0;
        const char *best = 0;
        for (int j = 0; j < n; j++) {
            const char *candidate = ops.get_section_name(&ops, tags[j]);
            size_t len = strlen(candidate);
            if (len > longest && strncmp(full, candidate, len) == 0) {
                longest = len;
                best = candidate;
            }
        }
        if (!best || strcmp(best, section) || longest + 1 >= strlen(full) ||
            strcmp(full + longest + 1, name)) {
            fprintf(stderr, "vendor tag does not round-trip: %s\n", full);
            return 1;
        }
        unsigned original_index = tags[i] - VTAG0;
        if (original_index >= NVTAGS || strcmp(section, g_vtags[original_index].section) ||
            strcmp(name, g_vtags[original_index].name) || ops.get_tag_type(&ops, tags[i]) != g_vtags[original_index].type ||
            vendor_tag(section, name) != tags[i]) {
            fprintf(stderr, "vendor tag id or type changed: %s\n", full);
            return 1;
        }
    }
    if (n != NVTAGS - 1) {
        fprintf(stderr, "expected exactly one ambiguous vendor tag to be excluded\n");
        return 1;
    }
    if (vendor_tag("xiaomi.hdr", "hdrChecker") != 0) {
        fprintf(stderr, "ambiguous vendor tag is still exposed\n");
        return 1;
    }
    puts("vendor tag name round-trip: OK");
    return 0;
}
