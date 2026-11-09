#ifndef RME_VIBRATION_BPROTO_STEP_H
#define RME_VIBRATION_BPROTO_STEP_H

#include "vibration_bproto_lifecycle.h"
#include "vibration_binder_contract.h"
#include "vibration_service_registration.h"
#include <pthread.h>
#include <stdint.h>

/* Offline-only model. Caller must authenticate and pin the binderd pidfd, and
 * peer_alive must nonblockingly validate that pinned identity. The callback is
 * never authorization for the Guest app. One worker owns next/complete;
 * stop may be called concurrently, and destroy only after workers have joined.
 * No hardware is accessed; caller must separately retire/cancel the actuator. */
/* REGISTERED is emitted once after the ServiceManager success reply; it is
 * not a vibration event and does not establish ownership or device support. */
enum { RME_VIB_STEP_STOPPED = 1, RME_VIB_STEP_EVENT = 2, RME_VIB_STEP_REGISTERED = 3 };
struct rme_vib_step_event { uint64_t token; uint32_t code; int32_t duration_ms; };
struct rme_vib_step {
    pthread_mutex_t lock;
    struct rme_bproto_client *client;
    struct rme_vib_lifecycle life;
    struct rme_vib_request pending;
    int (*peer_alive)(void *);
    void *peer_ctx;
    int stop_pipe[2], initialized, stopping, waiting, registered;
    int64_t deadline_ms;
    uint64_t serial, pending_token;
};
int rme_vib_step_init(struct rme_vib_step *, struct rme_bproto_client *,
                      const struct rme_vib_add_service_input *, int (*peer_alive)(void *), void *);
int rme_vib_step_next(struct rme_vib_step *, struct rme_vib_step_event *);
/* Accept only the event token just returned. False callback result closes the session. */
int rme_vib_step_complete(struct rme_vib_step *, uint64_t token, int callback_ok);
int rme_vib_step_stop(struct rme_vib_step *);
void rme_vib_step_destroy(struct rme_vib_step *);
#endif
