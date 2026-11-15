#ifndef REDMAGIC_AOD_RUNTIME_H
#define REDMAGIC_AOD_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#define AOD_SIDE 512u
#define AOD_MAX_DIMENSION 4096u
#define AOD_PIXELS (AOD_SIDE * AOD_SIDE)
#define AOD_INPUT_FLOATS (3u * AOD_PIXELS)

enum aod_result {
    AOD_OK = 0,
    AOD_INVALID_ARGUMENT = 1,
    AOD_NO_MEMORY = 2,
    AOD_INFERENCE_FAILED = 3,
    AOD_INVALID_OUTPUT = 4
};

struct aod_shape { size_t rank; int64_t dimensions[4]; };

/* Callback must write at most logits_capacity floats and set shape only on success. */
typedef int (*aod_infer_fn)(void *context, const float *input, size_t input_count,
                            float *logits, size_t logits_capacity, struct aod_shape *shape);

/* On failure, the caller's logits buffer is left unchanged. Input is packed RGB8. */
enum aod_result aod_infer_rgb(const uint8_t *rgb, size_t rgb_bytes, uint32_t width,
                              uint32_t height, aod_infer_fn infer, void *context,
                              float *logits, size_t logits_count);

#endif
