#ifndef RME_AOD_BACKEND_H
#define RME_AOD_BACKEND_H
#include <stddef.h>
#include <stdint.h>

/* Backend adapter returns zero only when initialized/inference succeeded.
 * Caller serializes access; logits capacity is exactly 512*512. */
int aod_backend_open(void **context);
int aod_backend_run(void *context, const uint8_t *rgb, size_t rgb_length,
                    int width, int height, float *logits, size_t capacity);
void aod_backend_close(void *context);
#endif
