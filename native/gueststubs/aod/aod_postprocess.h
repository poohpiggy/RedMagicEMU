#ifndef RME_AOD_POSTPROCESS_H
#define RME_AOD_POSTPROCESS_H
#include <stddef.h>
#include <stdint.h>

/* Prepare a binary model-resolution mask; caller supplies nonoverlapping
 * buffers, and lengths must reflect actual buffer sizes.  This stage does not
 * resize, erode, blur or compute a final image-resolution highest point. */
int aod_prepare_mask(const uint8_t *soft, size_t length, int width, int height,
                     uint8_t *binary, size_t binary_length);
/* Resize the binary mask, erode it, then blur it at target resolution. */
int aod_finish_mask(const uint8_t *binary, size_t length, int width, int height,
                    uint8_t *alpha, size_t alpha_length, int out_width, int out_height);
#endif
