#ifndef RME_VIBRATION_BINDER_PARCEL_H
#define RME_VIBRATION_BINDER_PARCEL_H

#include <stddef.h>
#include <stdint.h>
#include "vibration_binder_contract.h"

/* Offline decoder for the exact fixed Android 16 Guest Java AIDL samples.
 * Real Binder-driver offsets, other clients and versions are not verified.
 * The two dynamic caller metadata words are intentionally ignored; this is
 * not caller authentication. Do not connect to the product or host bridge. */
enum rme_vib_decode_status {
    RME_VIB_DECODE_OK = 0,
    RME_VIB_DECODE_INVALID,
    RME_VIB_DECODE_UNSUPPORTED,
};
enum rme_vib_decode_status rme_vib_decode(uint32_t code, uint32_t flags,
        const void *data, size_t data_size, size_t offsets_size,
        size_t extra_size, struct rme_vib_request *request);
#endif
