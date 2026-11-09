#include "../vibration_binder_contract.h"
#include <assert.h>
#include <stdio.h>

struct observed { int on; int off; int fail; int32_t duration; };
static int send_on(void *ctx, int32_t duration) {
    struct observed *o = ctx; o->on++; o->duration = duration; return o->fail;
}
static int send_off(void *ctx) {
    struct observed *o = ctx; o->off++; return o->fail;
}
int main(void) {
    struct observed o = {0};
    struct rme_vib_ops ops = { .ctx = &o, .on = send_on, .off = send_off };
    struct rme_vib_request request = { .code = 1 };
    struct rme_vib_reply reply;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK);
    assert(reply.capabilities == 0 && o.on == 0 && o.off == 0);
    for (uint32_t flags = 0; flags < 256; ++flags) {
        request.flags = flags;
        enum rme_vib_status expected = (flags == 0 || flags == 2 || flags == 16 || flags == 18)
                ? RME_VIB_OK : RME_VIB_INVALID;
        assert(rme_vib_dispatch(&request, &ops, &reply) == expected);
    }
    request.flags = UINT32_MAX;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.flags = 0;
    request.code = 3; request.duration_ms = 500;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK);
    assert(o.on == 1 && o.duration == 500);
    request.code = 2;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK && o.off == 1);
    request.code = 3; request.duration_ms = 0;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.duration_ms = -1;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.duration_ms = 5001;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.duration_ms = 1; request.callback_present = 1;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_UNSUPPORTED);
    assert(o.on == 1 && o.off == 1);
    request.callback_present = 0; request.flags = 1;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.flags = 16; /* observed on another target Guest synchronous transaction */
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK);
    assert(o.on == 2 && o.duration == 1);
    request.flags = 17; /* accept-fds does not make oneway acceptable */
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.flags = 2;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK);
    request.flags = 18;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK);
    request.flags = 4;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.flags = 32;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_INVALID);
    request.flags = 0; request.duration_ms = 5000;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_OK && o.duration == 5000);
    o.fail = 1;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_BRIDGE_FAILED);
    request.code = 2;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_BRIDGE_FAILED);
    int on = o.on, off = o.off;
    request.code = 4;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_UNSUPPORTED);
    request.code = 0xffffff;
    assert(rme_vib_dispatch(&request, &ops, &reply) == RME_VIB_UNSUPPORTED);
    assert(o.on == on && o.off == off);
    assert(rme_vib_dispatch(NULL, &ops, &reply) == RME_VIB_INVALID);
    assert(rme_vib_dispatch(&request, &ops, NULL) == RME_VIB_INVALID);
    puts("VIBRATION_CONTRACT_OK");
}
