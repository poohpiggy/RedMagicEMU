#include "../vibration_service_registration.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(void) {
    /* These inputs are deliberately synthetic, not a captured Guest Binder object. */
    struct rme_vib_add_service_input in = {
        .interface_header = {1,2,3,4,5,6,7,8,9,10,11,12},
        .binder_flags = 0x11223344u, .binder_ptr = UINT64_C(0x1020304050607080),
        .binder_cookie = UINT64_C(0x1122334455667788), .stability = 0x55667788u,
    };
    unsigned char parcel[200]; memset(parcel, 0xcc, sizeof parcel);
    size_t length = 0; uint64_t candidate_offset = 0;
    assert(rme_vib_encode_add_service(&in, parcel, sizeof parcel, &length, &candidate_offset) == 0);
    assert(length == 200 && candidate_offset == 164);
    assert(!memcmp(parcel, in.interface_header, 12));
    assert(le32(parcel + 12) == 26 && parcel[16] == 'a' && parcel[17] == 0);
    assert(le32(parcel + 72) == 43 && parcel[76] == 'a' && parcel[77] == 0);
    assert(le32(parcel + 164) == 0x73622a85u && le32(parcel + 168) == in.binder_flags);
    assert(le32(parcel + 188) == in.stability && le32(parcel + 192) == 0 && le32(parcel + 196) == 8);
    unsigned char before[200]; memset(before, 0xcc, sizeof before); memcpy(parcel, before, sizeof parcel);
    length = 19; candidate_offset = 33;
    assert(rme_vib_encode_add_service(&in, parcel, sizeof parcel - 1, &length, &candidate_offset) < 0);
    assert(!memcmp(parcel, before, sizeof before) && length == 19 && candidate_offset == 33);
    in.binder_ptr = 0;
    assert(rme_vib_encode_add_service(&in, parcel, sizeof parcel, &length, &candidate_offset) < 0);
    assert(!memcmp(parcel, before, sizeof before));
    puts("VIBRATION_SERVICE_REGISTRATION_ENCODING_OK");
    return 0;
}
