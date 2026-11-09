#include "../vibration_binder_reply.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char out[12]; size_t written;
    struct rme_vib_reply reply = { .capabilities = 0 };
    memset(out, 0xaa, sizeof out); written = 99;
    assert(rme_vib_encode_success(1, RME_VIB_OK, &reply, out, sizeof out, &written) == RME_VIB_ENCODE_OK);
    assert(written == 8);
    for (size_t i = 0; i < written; i++) assert(out[i] == 0);
    assert(out[8] == 0xaa);
    for (unsigned code = 2; code <= 3; code++) {
        memset(out, 0xaa, sizeof out); written = 99;
        assert(rme_vib_encode_success(code, RME_VIB_OK, &reply, out, sizeof out, &written) == RME_VIB_ENCODE_OK);
        assert(written == 4 && !out[0] && !out[1] && !out[2] && !out[3] && out[4] == 0xaa);
    }
    memset(out, 0xaa, sizeof out); written = 99;
    assert(rme_vib_encode_success(1, RME_VIB_OK, &reply, out, 7, &written) == RME_VIB_ENCODE_INVALID);
    assert(written == 0 && out[0] == 0xaa);
    for (int status = RME_VIB_INVALID; status <= RME_VIB_BRIDGE_FAILED; status++) {
        written = 99;
        assert(rme_vib_encode_success(3, status, &reply, out, sizeof out, &written) == RME_VIB_ENCODE_INVALID);
        assert(written == 0 && out[0] == 0xaa);
    }
    written = 99;
    assert(rme_vib_encode_success(4, RME_VIB_OK, &reply, out, sizeof out, &written) == RME_VIB_ENCODE_UNSUPPORTED);
    assert(written == 0 && out[0] == 0xaa);
    reply.capabilities = 1; written = 99;
    assert(rme_vib_encode_success(1, RME_VIB_OK, &reply, out, sizeof out, &written) == RME_VIB_ENCODE_INVALID);
    assert(written == 0 && out[0] == 0xaa);
    assert(rme_vib_encode_success(1, RME_VIB_OK, NULL, out, sizeof out, &written) == RME_VIB_ENCODE_INVALID);
    assert(rme_vib_encode_success(1, RME_VIB_OK, &reply, NULL, sizeof out, &written) == RME_VIB_ENCODE_INVALID);
    assert(rme_vib_encode_success(1, RME_VIB_OK, &reply, out, sizeof out, NULL) == RME_VIB_ENCODE_INVALID);
    puts("VIBRATION_REPLY_OK");
}
