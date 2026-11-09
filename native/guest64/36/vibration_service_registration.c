#include "vibration_service_registration.h"
#include <errno.h>
#include <string.h>

/* Offline-only ARM64 little-endian Parcel model. No service is published. */
static void put32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
static void put64(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
static size_t put_ascii_string16(unsigned char *p, const char *s) {
    size_t n = strlen(s);
    put32(p, (uint32_t)n);
    for (size_t i = 0; i < n; ++i) { p[4 + 2 * i] = (unsigned char)s[i]; p[5 + 2 * i] = 0; }
    p[4 + n * 2] = 0; p[5 + n * 2] = 0;
    size_t size = (4 + (n + 1) * 2 + 3) & ~(size_t)3;
    return size;
}

int rme_vib_encode_add_service(const struct rme_vib_add_service_input *in,
                               unsigned char *parcel, size_t capacity,
                               size_t *length, uint64_t *candidate_offset) {
    if (!in || !parcel || !length || !candidate_offset || capacity < 200 ||
        !in->binder_ptr || !in->binder_cookie) { errno = EINVAL; return -1; }
    unsigned char tmp[200] = {0};
    memcpy(tmp, in->interface_header, sizeof in->interface_header);
    size_t p = 12;
    p += put_ascii_string16(tmp + p, "android.os.IServiceManager");
    p += put_ascii_string16(tmp + p, "android.hardware.vibrator.IVibrator/default");
    const size_t object = p;
    put32(tmp + p, 0x73622a85u); p += 4; /* BINDER_TYPE_BINDER */
    put32(tmp + p, in->binder_flags); p += 4;
    put64(tmp + p, in->binder_ptr); p += 8;
    put64(tmp + p, in->binder_cookie); p += 8;
    put32(tmp + p, in->stability); p += 4;
    put32(tmp + p, 0); p += 4; /* allowIsolated = false */
    put32(tmp + p, 8); p += 4; /* captured dumpPriority argument */
    if (p != sizeof tmp || object != 164) { errno = EINVAL; return -1; }
    memcpy(parcel, tmp, sizeof tmp);
    *length = sizeof tmp;
    *candidate_offset = object;
    return 0;
}

int rme_vib_encode_target_add_service(const struct rme_vib_target_registration_input *in,
                                      unsigned char *parcel, size_t capacity,
                                      size_t *length, uint64_t *candidate_offset) {
    static const unsigned char syst[4] = {'T', 'S', 'Y', 'S'};
    if (!in || in->experimental_enabled != 1 || in->api != 36 || in->n64 != 1 ||
        !in->model || strcmp(in->model, "NX809J") || !in->abi || strcmp(in->abi, "arm64-v8a") ||
        memcmp(in->transaction.interface_header + 8, syst, sizeof syst) ||
        in->transaction.binder_flags != 0x113 || in->transaction.stability != 63) {
        errno = EINVAL;
        return -1;
    }
    return rme_vib_encode_add_service(&in->transaction, parcel, capacity, length, candidate_offset);
}
