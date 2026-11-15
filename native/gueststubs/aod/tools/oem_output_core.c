#include "../aod_mask.h"
#include "../aod_postprocess.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define MODEL_SIDE 640
#define MODEL_PIXELS ((size_t)MODEL_SIDE * MODEL_SIDE)

/* OEM CV_32F convertTo(CV_8U, 255, 0): finite values only; cvRound uses
 * nearest-even. Values outside [0,1] saturate before conversion. */
int oem_float_to_u8(const float *input, size_t count, uint8_t *out) {
    if (!input || !out) return -1;
    for (size_t i = 0; i < count; ++i) if (!isfinite(input[i])) return -1;
    for (size_t i = 0; i < count; ++i) {
        float x = input[i];
        if (x <= 0.0f) out[i] = 0;
        else if (x >= 1.0f) out[i] = 255;
        else out[i] = (uint8_t)nearbyintf(x * 255.0f);
    }
    return 0;
}

struct inference_input { const float *values; };
static int finish(void *opaque, const uint8_t *rgb, int w, int h,
                  uint8_t *alpha, size_t pixels) {
    (void)rgb;
    const struct inference_input *input = opaque;
    uint8_t *soft = malloc(MODEL_PIXELS), *binary = malloc(MODEL_PIXELS);
    int ok = -1;
    if (soft && binary && !oem_float_to_u8(input->values, MODEL_PIXELS, soft) &&
        !aod_prepare_mask(soft, MODEL_PIXELS, MODEL_SIDE, MODEL_SIDE, binary, MODEL_PIXELS) &&
        !aod_finish_mask(binary, MODEL_PIXELS, MODEL_SIDE, MODEL_SIDE, alpha, pixels, w, h)) ok = 0;
    free(soft); free(binary);
    return ok;
}

int oem_process(const float *model, size_t count, const uint8_t *rgb,
                size_t rgb_bytes, int w, int h, uint8_t *bgra,
                size_t bgra_bytes, int *x, int *y) {
    if (!model || count != MODEL_PIXELS || !x || !y) return -1;
    struct inference_input input = { model };
    struct aod_mask_state state;
    aod_mask_init(&state, finish, &input);
    int result = aod_mask_run(&state, rgb, rgb_bytes, w, h, bgra, bgra_bytes);
    *x = state.top_x; *y = state.top_y;
    aod_mask_release(&state);
    return result;
}
