#include "vibration_binder_parcel.h"

#include <string.h>

/* The target Java sample contains the standard system libbinder header.
 * Accept no other header until an actual target sender is independently seen. */
enum { TOKEN_SIZE = 88, ON_SIZE = 120, PERFORM_SIZE = 124, MAX_DURATION_MS = 5000 };
#define SYSTEM_HEADER 0x53595354u
#define FLAT_BINDER_TYPE 0x73622a85u
#define NULL_BINDER_FLAGS 0x13u

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int token_valid(const unsigned char *p, size_t size) {
    static const char descriptor[] = "android.hardware.vibrator.IVibrator";
    if (size < TOKEN_SIZE || le32(p + 8) != SYSTEM_HEADER ||
        le32(p + 12) != sizeof(descriptor) - 1) return 0;
    for (size_t i = 0; i < sizeof(descriptor) - 1; ++i)
        if (p[16 + 2*i] != (unsigned char)descriptor[i] || p[17 + 2*i] != 0) return 0;
    return p[86] == 0 && p[87] == 0;
}

static int null_binder_valid(const unsigned char *p) {
    uint32_t flags = le32(p + 4);
    static const unsigned char zero[20] = {0};
    return le32(p) == FLAT_BINDER_TYPE &&
        (flags == 0 || flags == NULL_BINDER_FLAGS) &&
        memcmp(p + 8, zero, sizeof zero) == 0;
}

enum rme_vib_decode_status rme_vib_decode(uint32_t code, uint32_t flags,
        const void *data, size_t data_size, size_t offsets_size,
        size_t extra_size, struct rme_vib_request *request) {
    if (!data || !request || (flags & ~RME_VIB_ALLOWED_SYNC_FLAGS) || offsets_size != 0 || extra_size != 0)
        return RME_VIB_DECODE_INVALID;
    const unsigned char *p = data;
    if (!token_valid(p, data_size)) return RME_VIB_DECODE_INVALID;
    struct rme_vib_request decoded = { .code = code, .flags = flags };
    switch (code) {
    case 1: /* getCapabilities */
    case 2: /* off */
    case 5: case 10: /* empty supported-feature arrays */
    case 8: case 9: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 26: case 27: case 28: case 29: /* unsupported optional features */
        if (data_size != TOKEN_SIZE) return RME_VIB_DECODE_INVALID;
        break;
    case 3: { /* on(int, null callback); only this sampled form is accepted */
        if (data_size != ON_SIZE) return RME_VIB_DECODE_INVALID;
        uint32_t duration = le32(p + TOKEN_SIZE);
        if (duration == 0 || duration > MAX_DURATION_MS) return RME_VIB_DECODE_INVALID;
        if (!null_binder_valid(p + 92)) return RME_VIB_DECODE_INVALID;
        decoded.duration_ms = (int32_t)duration;
        decoded.callback_present = 0;
        break;
    }
    case 4: /* perform(effect, strength, null callback): unsupported */
        if (data_size != PERFORM_SIZE || !null_binder_valid(p + 96))
            return RME_VIB_DECODE_INVALID;
        break;
    default:
        return RME_VIB_DECODE_UNSUPPORTED;
    }
    *request = decoded;
    return RME_VIB_DECODE_OK;
}
