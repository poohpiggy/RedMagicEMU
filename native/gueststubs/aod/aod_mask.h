#ifndef RME_AOD_MASK_H
#define RME_AOD_MASK_H

#include <stddef.h>
#include <stdint.h>

/* Backend writes an already postprocessed, full-resolution alpha plane and
 * returns 0 only on successful inference. The caller serializes each state.
 * Buffers must not overlap and their reported lengths must be truthful.
 * Supported images have at most 16,777,216 pixels. On failure, a nonoverlapping
 * BGRA buffer of at most 64 MiB is zeroed. Invalid/overlapping buffers and
 * oversized output buffers are never touched and must not be consumed. */
typedef int (*aod_mask_infer)(void *opaque, const uint8_t *rgb,
                              int width, int height, uint8_t *alpha,
                              size_t pixels);

struct aod_mask_state {
    aod_mask_infer infer;
    void *opaque;
    int has_alpha;
    int top_x;  /* -1 when no mask */
    int top_y;  /* image height when no mask, if dimensions were valid */
};

void aod_mask_init(struct aod_mask_state *state, aod_mask_infer infer, void *opaque);
int aod_mask_run(struct aod_mask_state *state, const uint8_t *rgb, size_t rgb_length,
                 int width, int height, uint8_t *bgra, size_t bgra_length);
void aod_mask_release(struct aod_mask_state *state);

#endif
