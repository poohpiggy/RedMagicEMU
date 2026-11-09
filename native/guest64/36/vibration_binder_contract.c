#include "vibration_binder_contract.h"

/* Transaction ordinals 1/2/3 follow the checked Android 16 AIDL v2 snapshot.
 * Structured input must come from a separately verified Parcel decoder. */
enum { GET_CAPABILITIES = 1, OFF = 2, ON = 3, MAX_DURATION_MS = 5000 };

enum rme_vib_status rme_vib_dispatch(const struct rme_vib_request *request,
                                      const struct rme_vib_ops *ops,
                                      struct rme_vib_reply *reply) {
    if (!request || !reply) return RME_VIB_INVALID;
    reply->capabilities = 0;
    if (request->flags & ~RME_VIB_ALLOWED_SYNC_FLAGS) return RME_VIB_INVALID;
    switch (request->code) {
    case GET_CAPABILITIES:
        return RME_VIB_OK;
    case OFF:
        if (!ops || !ops->off) return RME_VIB_BRIDGE_FAILED;
        return ops->off(ops->ctx) == 0 ? RME_VIB_OK : RME_VIB_BRIDGE_FAILED;
    case ON:
        if (request->duration_ms < 1 || request->duration_ms > MAX_DURATION_MS)
            return RME_VIB_INVALID;
        /* Callback semantics are not implemented; never claim completion. */
        if (request->callback_present) return RME_VIB_UNSUPPORTED;
        if (!ops || !ops->on) return RME_VIB_BRIDGE_FAILED;
        return ops->on(ops->ctx, request->duration_ms) == 0 ? RME_VIB_OK : RME_VIB_BRIDGE_FAILED;
    default:
        return RME_VIB_UNSUPPORTED;
    }
}
