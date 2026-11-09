#ifndef RME_VIBRATION_BINDER_CONTRACT_H
#define RME_VIBRATION_BINDER_CONTRACT_H

#include <stdint.h>

/* Offline operation policy only; no Parcel decoding, Binder registration,
 * caller authentication or device access. Never treat it as a working HAL. */
/* Target Guest code 1 observed 0x12; off/on driver flags remain unobserved.
 * Accepting these bits implements neither Guest AppOps nor FD transfer. */
#define RME_VIB_TF_COLLECT_NOTED_APP_OPS UINT32_C(0x02)
#define RME_VIB_TF_ACCEPT_FDS UINT32_C(0x10)
#define RME_VIB_ALLOWED_SYNC_FLAGS (RME_VIB_TF_COLLECT_NOTED_APP_OPS | RME_VIB_TF_ACCEPT_FDS)
enum rme_vib_status {
    RME_VIB_OK = 0,
    RME_VIB_INVALID,
    RME_VIB_UNSUPPORTED,
    RME_VIB_BRIDGE_FAILED,
};

struct rme_vib_request {
    uint32_t code;
    uint32_t flags;
    int32_t duration_ms;
    int callback_present;
};
struct rme_vib_reply { int32_t capabilities; };
struct rme_vib_ops {
    void *ctx;
    int (*on)(void *ctx, int32_t duration_ms);
    int (*off)(void *ctx);
};

enum rme_vib_status rme_vib_dispatch(const struct rme_vib_request *request,
                                      const struct rme_vib_ops *ops,
                                      struct rme_vib_reply *reply);
#endif
