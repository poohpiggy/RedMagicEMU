#ifndef RME_SIX_SHA256_H
#define RME_SIX_SHA256_H
#include <stddef.h>
#include <stdint.h>
/* Experimental one-shot SHA-256. data may be NULL only if length is zero. The input
 * length must be <= UINT64_MAX / 8. */
void rme_sha256(const uint8_t *data, size_t length, uint8_t out[32]);
#endif
