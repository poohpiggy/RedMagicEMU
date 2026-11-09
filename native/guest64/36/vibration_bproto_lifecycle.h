#ifndef RME_VIBRATION_BPROTO_LIFECYCLE_H
#define RME_VIBRATION_BPROTO_LIFECYCLE_H

#include "vibration_bproto_client.h"
#include "bproto.h"
#include <stddef.h>
#include <stdint.h>

/* Offline-only single-node protocol helper. It does not authenticate the
 * peer, register a service, dispatch calls, or authorize Guest clients.
 * A buffer id is a daemon-side pointer: never use this helper without first
 * authenticating the daemon's live pidfd as well as the socket peer. */
struct rme_vib_lifecycle {
    struct rme_bproto_client *client;
    uint64_t ptr, cookie, pending_id;
    int entered;
};

void rme_vib_lifecycle_init(struct rme_vib_lifecycle *, struct rme_bproto_client *, uint64_t, uint64_t);
int rme_vib_lifecycle_enter(struct rme_vib_lifecycle *);
int rme_vib_lifecycle_observe(struct rme_vib_lifecycle *, const struct d_hdr *, const void *, size_t);
/* Free the single received buffer only after its contents are no longer used. */
int rme_vib_lifecycle_free(struct rme_vib_lifecycle *);
/* Exit the looper, then close the client to retire its Binder node. */
int rme_vib_lifecycle_exit(struct rme_vib_lifecycle *);
#endif
