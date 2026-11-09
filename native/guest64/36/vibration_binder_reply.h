#ifndef RME_VIBRATION_BINDER_REPLY_H
#define RME_VIBRATION_BINDER_REPLY_H

#include <stddef.h>
#include <stdint.h>
#include "vibration_binder_contract.h"

/* Offline success replies for the three fixed Java proxy samples only.
 * Binder transport, status exceptions and caller authorization are not here. */
enum rme_vib_encode_status {
    RME_VIB_ENCODE_OK = 0,
    RME_VIB_ENCODE_INVALID,
    RME_VIB_ENCODE_UNSUPPORTED,
};
enum rme_vib_encode_status rme_vib_encode_success(uint32_t code,
        enum rme_vib_status status, const struct rme_vib_reply *reply,
        void *output, size_t capacity, size_t *written);
#endif
