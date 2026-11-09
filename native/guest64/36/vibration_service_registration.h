#ifndef RME_VIBRATION_SERVICE_REGISTRATION_H
#define RME_VIBRATION_SERVICE_REGISTRATION_H
#include <stddef.h>
#include <stdint.h>

/* Offline model only. The 12-byte header, flags and stability representation
 * have no production defaults and must come from a separately verified target.
 * The returned object offset is a candidate based on Parcel byte positions;
 * it has NOT been confirmed as a real cross-process Binder offsets entry. */
struct rme_vib_add_service_input {
    unsigned char interface_header[12];
    uint32_t binder_flags;
    uint64_t binder_ptr, binder_cookie;
    uint32_t stability;
};

int rme_vib_encode_add_service(const struct rme_vib_add_service_input *,
                               unsigned char *parcel, size_t capacity,
                               size_t *length, uint64_t *candidate_offset);

/* Explicit, offline target gate. The 0x113/63 values were observed for a
 * target Guest's VINTF-marked Java test Binder; the offset 164 is inferred
 * from a measured diagnostic-name offset and the UTF-16 name length. Neither
 * proves real registration or other implementations' object metadata. The
 * routing fields do not authenticate the ROM; admission is a separate check. */
struct rme_vib_target_registration_input {
    const char *model, *abi;
    unsigned api;
    int n64, experimental_enabled;
    struct rme_vib_add_service_input transaction;
};
int rme_vib_encode_target_add_service(const struct rme_vib_target_registration_input *,
                                      unsigned char *parcel, size_t capacity,
                                      size_t *length, uint64_t *candidate_offset);
#endif
