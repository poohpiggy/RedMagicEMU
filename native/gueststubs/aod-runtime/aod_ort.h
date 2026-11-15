#ifndef REDMAGIC_AOD_ORT_H
#define REDMAGIC_AOD_ORT_H

#include "aod_runtime.h"
#include <onnxruntime_c_api.h>

struct aod_ort;
/* An instance is not thread-safe: the caller must serialize infer and destroy. */
enum aod_result aod_ort_create(const OrtApi *api, const char *model_path,
                               struct aod_ort **out, char *error, size_t error_capacity);
void aod_ort_destroy(struct aod_ort *context);
const char *aod_ort_error(const struct aod_ort *context);
/* Implements aod_infer_fn; diagnostics for a failed call are in aod_ort_error. */
int aod_ort_infer(void *context, const float *input, size_t input_count,
                  float *logits, size_t logits_capacity, struct aod_shape *shape);

#endif
