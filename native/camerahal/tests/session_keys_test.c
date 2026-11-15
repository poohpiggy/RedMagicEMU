/* Exercise both static_info metadata outputs without Android's metadata library. */
#include <stdio.h>
#include "../camera_hal3.c"

/* android.request.availableSessionKeys, added after this HAL's tag header. */
#define SESSION_KEYS_TAG 0x000c0010

struct metadata_capture {
    int entries;
    size_t count;
    int has_data;
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
    if (tag == SESSION_KEYS_TAG) {
        m->entries++;
        m->count = count;
        m->has_data = data != 0;
    } else if (tag == REQUEST_AVAILABLE_CHARACTERISTICS_KEYS) {
        const int32_t *keys = data;
        for (size_t i = 0; i < count; i++)
            if ((uint32_t)keys[i] == SESSION_KEYS_TAG) m->listed++;
    }
    return 0;
}

int main(void) {
    md_alloc = capture_alloc;
    md_add = capture_add;
    for (int id = 0; id < 2; id++) {
        const struct metadata_capture *m = static_info(id);
        if (!m || m->entries != 1 || m->count != 0 || !m->has_data || m->listed != 1) {
            fprintf(stderr, "camera %d: expected an advertised empty session key list\n", id);
            return 1;
        }
    }
    puts("empty session key metadata: OK");
    return 0;
}
