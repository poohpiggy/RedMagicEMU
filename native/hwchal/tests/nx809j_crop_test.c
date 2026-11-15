#define AEMU_NX809J_DYNAMIC_REFRESH 1
#include "../hwc.c"

int hw_get_module(const char *id, const void **module) { (void)id; (void)module; return -1; }

static int lock_buffer(const struct gralloc_module *m, const void *handle, int usage,
                       int l, int t, int w, int h, void **vaddr) {
    (void)m; (void)usage; (void)l; (void)t; (void)w; (void)h;
    *vaddr = (void *)((const struct rgb_note_handle *)handle)->base;
    return 0;
}
static int unlock_buffer(const struct gralloc_module *m, const void *handle) {
    (void)m; (void)handle; return 0;
}
static void set_crop(struct layer *layer, float l, float t, float r, float b) {
    float crop[] = { l, t, r, b };
    memcpy(&layer->crop, crop, sizeof(crop));
}

int main(void) {
    union { uint64_t align; uint8_t bytes[4096]; } buffer = {0};
    uint32_t *pixels = (uint32_t *)buffer.bytes;
    for (int i = 0; i < 16; i++) pixels[i] = 0xff000000u | (uint32_t)i;
    struct rgb_note *note = (struct rgb_note *)(buffer.bytes + sizeof(buffer.bytes) - sizeof(*note));
    *note = (struct rgb_note){ .magic = RGB_NOTE, .w = 4, .h = 4, .stride = 4, .format = FMT_RGBX };
    struct rgb_note_handle handle = { .size = sizeof(buffer.bytes), .base = (uint64_t)buffer.bytes };
    struct gralloc_module gralloc = { .lock = lock_buffer, .unlock = unlock_buffer };
    uint32_t result[4] = {0};
    struct hwc d = { .gralloc = &gralloc, .w = 2, .h = 2, .back = result };
    struct layer layer = { .type = L_FRAMEBUFFER, .handle = &handle, .blending = BLEND_NONE,
                           .frame = {0, 0, 2, 2}, .plane_alpha = 255 };
    set_crop(&layer, 1.0f, 1.0f, 3.0f, 3.0f);
    if (!plain(&d, &layer)) return 1;
    draw(&d, &layer, note);
    if (result[0] != pixels[5] || result[1] != pixels[6] ||
        result[2] != pixels[9] || result[3] != pixels[10]) return 2;
    set_crop(&layer, 1.5f, 1.0f, 3.5f, 3.0f);
    if (plain(&d, &layer)) return 3;
    set_crop(&layer, -1.0f, 1.0f, 1.0f, 3.0f);
    if (plain(&d, &layer)) return 4;
    set_crop(&layer, 2147483648.0f, 1.0f, 2147483648.0f, 3.0f);
    if (plain(&d, &layer)) return 5;
    return 0;
}
