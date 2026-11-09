#ifndef RME_QTI_SESSION_H
#define RME_QTI_SESSION_H
#include "qti_led_adapter.h"
#include <stdint.h>
#include <sys/types.h>

/* EXPERIMENT ONLY: verifies credentials on an RMEH handshake using recvmsg;
 * this is not yet verified under run64/Android and must not be loaded in Guest.
 * Caller supplies trusted host credentials and must independently
 * authorize Guest requests. No function here establishes application authority.
 * Zero-initialize this struct before its first start. One session per process.
 * Calls to start/close are not concurrent; after close,
 * all HAL callers must stop before the session object is released. */
struct rme_qti_session {
    struct rme_guest_vibration_sender sender;
    struct rme_qti_led_adapter adapter;
    int active;
};
int rme_qti_session_start(struct rme_qti_session *, const char *abstract_name,
                          pid_t host_pid, uid_t host_uid, uint64_t epoch);
int rme_qti_session_close(struct rme_qti_session *);
#endif
