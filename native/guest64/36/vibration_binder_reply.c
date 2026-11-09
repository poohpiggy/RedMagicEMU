#include "vibration_binder_reply.h"
#include <string.h>

enum rme_vib_encode_status rme_vib_encode_success(uint32_t code,
        enum rme_vib_status status, const struct rme_vib_reply *reply,
        void *output, size_t capacity, size_t *written) {
    if (!written) return RME_VIB_ENCODE_INVALID;
    *written = 0;
    /* Failure replies have not been captured; never report them as success. */
    if (status != RME_VIB_OK || !reply || !output) return RME_VIB_ENCODE_INVALID;
    size_t required;
    switch (code) {
    case 1:
        if (reply->capabilities != 0) return RME_VIB_ENCODE_INVALID;
        required = 8; /* writeNoException, capability=0 */
        break;
    case 5: case 10:
        required = 8; /* writeNoException, empty supported-feature array */
        break;
    case 2:
    case 3:
        required = 4; /* writeNoException */
        break;
    default:
        return RME_VIB_ENCODE_UNSUPPORTED;
    }
    if (capacity < required) return RME_VIB_ENCODE_INVALID;
    memset(output, 0, required);
    *written = required;
    return RME_VIB_ENCODE_OK;
}
