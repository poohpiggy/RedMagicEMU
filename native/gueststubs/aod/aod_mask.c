#include "aod_mask.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AOD_MAX_PIXELS ((size_t)16777216)

static int overlaps(const void *a, size_t alen, const void *b, size_t blen) {
    if (!a || !b || !alen || !blen) return 0;
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < alen : x - y < blen;
}

static void clear_result(struct aod_mask_state *state, int height) {
    state->has_alpha = 0;
    state->top_x = -1;
    state->top_y = height > 0 ? height : 0;
}

void aod_mask_init(struct aod_mask_state *state, aod_mask_infer infer, void *opaque) {
    if (!state) return;
    state->infer = infer;
    state->opaque = opaque;
    clear_result(state, 0);
}

int aod_mask_run(struct aod_mask_state *state, const uint8_t *rgb, size_t rgb_length,
                 int width, int height, uint8_t *bgra, size_t bgra_length) {
    if (state) clear_result(state, height);
    if (!bgra || bgra_length > AOD_MAX_PIXELS * 4 ||
        overlaps(rgb, rgb_length, bgra, bgra_length)) return -1;
    memset(bgra, 0, bgra_length);
    if (!state || !state->infer || !rgb || width <= 0 || height <= 0) return -1;
    size_t w = (size_t)width, h = (size_t)height;
    if (w > SIZE_MAX / h) return -1;
    size_t pixels = w * h;
    if (pixels > AOD_MAX_PIXELS || pixels > SIZE_MAX / 4 || pixels > SIZE_MAX / 3 ||
        rgb_length != pixels * 3 || bgra_length != pixels * 4) return -1;
    uint8_t *alpha = calloc(pixels, 1);
    if (!alpha) return -1;
    if (state->infer(state->opaque, rgb, width, height, alpha, pixels) != 0) {
        free(alpha);
        return -1;
    }
    size_t first = 0;
    while (first < pixels && alpha[first] == 0) ++first;
    if (first == pixels) {
        free(alpha);
        return -1;
    }
    for (size_t i = 0; i < pixels; ++i) {
        bgra[i * 4] = rgb[i * 3 + 2];
        bgra[i * 4 + 1] = rgb[i * 3 + 1];
        bgra[i * 4 + 2] = rgb[i * 3];
        bgra[i * 4 + 3] = alpha[i];
    }
    state->top_x = (int)(first % w);
    state->top_y = (int)(first / w);
    state->has_alpha = 1;
    free(alpha);
    return 0;
}

void aod_mask_release(struct aod_mask_state *state) {
    if (!state) return;
    state->infer = NULL;
    state->opaque = NULL;
    clear_result(state, 0);
}
