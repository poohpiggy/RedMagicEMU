#include "../aod_mask.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct backend { int result; uint8_t alpha[4]; };

static int infer(void *opaque, const uint8_t *rgb, int width, int height,
                 uint8_t *alpha, size_t pixels) {
    struct backend *backend = opaque;
    assert(rgb && width == 2 && height == 2 && pixels == 4);
    memcpy(alpha, backend->alpha, pixels);
    return backend->result;
}

int main(void) {
    const uint8_t rgb[] = {1,2,3, 4,5,6, 7,8,9, 10,11,12};
    const uint8_t expected[] = {3,2,1,0, 6,5,4,17, 9,8,7,255, 12,11,10,0};
    uint8_t out[16];
    struct backend backend = {0, {0,17,255,0}};
    struct aod_mask_state state;
    aod_mask_init(&state, infer, &backend);
    assert(!state.has_alpha);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) == 0);
    assert(memcmp(out, expected, sizeof(out)) == 0);
    assert(state.has_alpha && state.top_x == 1 && state.top_y == 0);
    backend.result = -1;
    memset(out, 0xaa, sizeof(out));
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) != 0);
    assert(!state.has_alpha && out[0] == 0 && out[15] == 0);
    backend.result = 0;
    memset(backend.alpha, 0, sizeof(backend.alpha));
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) != 0);
    assert(!state.has_alpha);
    backend.alpha[2] = 1;
    assert(aod_mask_run(&state, rgb, sizeof(rgb) - 1, 2, 2, out, sizeof(out)) != 0);
    assert(!state.has_alpha);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out) - 1) != 0);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), -1, 2, out, sizeof(out)) != 0);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) == 0);
    assert(state.has_alpha && state.top_x == 0 && state.top_y == 1);
    assert(aod_mask_run(&state, rgb, (size_t)4097 * 4096 * 3, 4097, 4096,
                        out, (size_t)4097 * 4096 * 4) != 0);
    assert(!state.has_alpha);
    uint8_t shared[16] = {1,2,3,4,5,6,7,8,9,10,11,12};
    assert(aod_mask_run(&state, shared, 12, 2, 2, shared, 16) != 0);
    assert(!state.has_alpha && shared[0] == 1 && shared[1] == 2);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), INT_MAX, INT_MAX, out, sizeof(out)) != 0);
    assert(!state.has_alpha && state.top_x == -1 && state.top_y == INT_MAX);
    assert(aod_mask_run(&state, NULL, sizeof(rgb), 2, 2, out, sizeof(out)) != 0);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, NULL, sizeof(out)) != 0);
    memset(out, 0x55, sizeof(out));
    assert(aod_mask_run(NULL, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) != 0);
    assert(out[0] == 0 && out[15] == 0);
    aod_mask_release(&state);
    assert(!state.has_alpha);
    assert(aod_mask_run(&state, rgb, sizeof(rgb), 2, 2, out, sizeof(out)) != 0);
    puts("AOD_MASK_OK");
    return 0;
}
