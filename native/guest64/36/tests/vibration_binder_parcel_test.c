#include "../vibration_binder_parcel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Captured from the target Guest fixed local Java Binder proxy. Its first two
 * dynamic header words were deliberately zeroed; offsets were not captured. */
static const char *const token =
    "00000000000000005453595323000000"
    "61006e00640072006f00690064002e00"
    "68006100720064007700610072006500"
    "2e007600690062007200610074006f00"
    "72002e00490056006900620072006100"
    "74006f0072000000";
static const char *const on_tail =
    "f4010000852a62731300000000000000"
    "00000000000000000000000000000000";
static unsigned digit(char c) { return (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10); }
static int fake_on(void *ctx, int32_t duration) {
    int32_t *last = ctx;
    *last = duration;
    return 0;
}
static size_t unhex(unsigned char *out, const char *hex) {
    size_t n = strlen(hex); assert(!(n & 1));
    for (size_t i = 0; i < n / 2; ++i) out[i] = (unsigned char)((digit(hex[2*i]) << 4) | digit(hex[2*i+1]));
    return n / 2;
}
int main(void) {
    unsigned char p[128];
    struct rme_vib_request request;
    const size_t n = unhex(p, token);
    assert(n == 88);
    assert(rme_vib_decode(1, 0, p, n, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.code == 1 && request.flags == 0);
    /* Kernel-wire TF_ACCEPT_FDS is valid for a synchronous call; it does not
     * authorize an actual descriptor in the message or its reply. */
    assert(rme_vib_decode(1, 0x10, p, n, 0, 0, &request) == RME_VIB_DECODE_OK && request.flags == 0x10);
    for (uint32_t flags = 0; flags < 256; ++flags) {
        enum rme_vib_decode_status expected = (flags == 0 || flags == 2 || flags == 16 || flags == 18)
                ? RME_VIB_DECODE_OK : RME_VIB_DECODE_INVALID;
        assert(rme_vib_decode(1, flags, p, n, 0, 0, &request) == expected);
    }
    /* Target Guest code 1 observed 0x12 on the Binder wire; code 2/3 not yet observed. */
    assert(rme_vib_decode(1, 18, p, n, 0, 0, &request) == RME_VIB_DECODE_OK && request.flags == 18);
    assert(rme_vib_decode(1, UINT32_MAX, p, n, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(2, 0, p, n, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.code == 2);
    assert(n + unhex(p + n, on_tail) == 120);
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.code == 3 && request.duration_ms == 500 && !request.callback_present);
    assert(rme_vib_decode(3, 0x10, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK && request.flags == 0x10);
    /* The target's unrelated synchronous addService used TF_ACCEPT_FDS.
     * Vibration transactions' driver flags have not been captured yet. */
    assert(rme_vib_decode(3, 16, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.flags == 16 && request.duration_ms == 500);
    assert(rme_vib_decode(3, 18, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.flags == 18 && request.duration_ms == 500);
    /* Target native client: on(2000, null), no scheduler flags on null Binder. */
    p[88] = 0xd0; p[89] = 0x07; p[96] = 0;
    assert(rme_vib_decode(3, 0x10, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.duration_ms == 2000 && !request.callback_present);
    p[88] = 0xf4; p[89] = 1; p[96] = 0x13;
    assert(rme_vib_decode(3, 0x10, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    int32_t last = 0;
    struct rme_vib_ops ops = { .ctx = &last, .on = fake_on };
    struct rme_vib_reply reply;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK && last == 500);
    /* Every truncated prefix must fail before reading beyond its buffer. */
    for (size_t i = 0; i < 120; ++i)
        assert(rme_vib_decode(3, 0, p, i, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    for (size_t i = 0; i < 88; ++i)
        assert(rme_vib_decode(1, 0, p, i, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    request.code = 12345;
    assert(rme_vib_decode(3, 0, p, 119, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(request.code == 12345); /* invalid payload never emits a request */
    p[0] = 0xfe; p[4] = 0xff; /* caller metadata is not used for authorization */
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(rme_vib_decode(3, 1, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0x11, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0x20, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 17, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 4, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 32, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0, p, 119, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0, p, 121, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(1, 0, p, 87, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(1, 0, p, 89, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0, p, 120, 8, 0, &request) == RME_VIB_DECODE_INVALID); /* offsets unknown: reject */
    assert(rme_vib_decode(3, 16, p, 120, 8, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 18, p, 120, 8, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 16, p, 120, 0, 1, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(3, 0, p, 120, 0, 1, &request) == RME_VIB_DECODE_INVALID);
    for (size_t i = 8; i < 88; ++i) {
        p[i] ^= 1;
        assert(rme_vib_decode(1, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID);
        p[i] ^= 1;
    }
    for (size_t i = 92; i < 120; ++i) {
        p[i] ^= 1;
        assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
        p[i] ^= 1;
    }
    p[88] = 1; p[89] = 0;
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK && request.duration_ms == 1);
    p[88] = 0x88; p[89] = 0x13;
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_OK && request.duration_ms == 5000);
    p[88] = 0xf4; p[89] = 1;
    p[8] ^= 1; assert(rme_vib_decode(1, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[8] ^= 1;
    p[12] ^= 1; assert(rme_vib_decode(1, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[12] ^= 1;
    p[16] ^= 1; assert(rme_vib_decode(1, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[16] ^= 1;
    p[86] = 1; assert(rme_vib_decode(1, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[86] = 0;
    p[92] ^= 1; assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[92] ^= 1;
    p[96] ^= 1; assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[96] ^= 1;
    p[100] = 1; assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[100] = 0;
    p[108] = 1; assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[108] = 0;
    p[116] = 1; assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID); p[116] = 0;
    memset(p + 88, 0, 4);
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    p[88] = 0x89; p[89] = 0x13; /* 5001 */
    assert(rme_vib_decode(3, 0, p, 120, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(4, 0, p, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    unsigned char perform[124] = {0};
    unhex(perform, token);
    perform[88] = 1; perform[92] = 2; /* effect and strength */
    unhex(perform + 96, on_tail + 8); /* null callback */
    assert(rme_vib_decode(4, 0x10, perform, sizeof perform, 0, 0, &request) == RME_VIB_DECODE_OK);
    assert(request.code == 4 && !request.callback_present);
    perform[100] = 0;
    assert(rme_vib_decode(4, 0x10, perform, sizeof perform, 0, 0, &request) == RME_VIB_DECODE_OK);
    for (size_t i = 0; i < sizeof perform; ++i)
        assert(rme_vib_decode(4, 0, perform, i, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(4, 0, perform, sizeof perform, 8, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(4, 0, perform, sizeof perform, 0, 1, &request) == RME_VIB_DECODE_INVALID);
    for (size_t i = 96; i < sizeof perform; ++i) {
        perform[i] ^= 1;
        assert(rme_vib_decode(4, 0, perform, sizeof perform, 0, 0, &request) == RME_VIB_DECODE_INVALID);
        perform[i] ^= 1;
    }
    assert(rme_vib_decode(1, 0, NULL, 88, 0, 0, &request) == RME_VIB_DECODE_INVALID);
    assert(rme_vib_decode(1, 0, p, 88, 0, 0, NULL) == RME_VIB_DECODE_INVALID);
    puts("VIBRATION_PARCEL_OK");
}
