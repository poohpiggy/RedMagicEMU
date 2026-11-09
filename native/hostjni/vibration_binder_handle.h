#ifndef RME_VIBRATION_BINDER_HANDLE_H
#define RME_VIBRATION_BINDER_HANDLE_H
#include "../guest64/36/vibration_bproto_step.h"
#include <stdint.h>
#include <sys/types.h>

/* Opaque monotonically allocated tokens, not addresses. Offline experiment:
 * caller must be the authenticated VM owner and serialize actuator stop with
 * callback execution. No API here grants Guest app permission. */
uint64_t rme_vib_handle_open(int socket_fd, int pidfd, pid_t pid, uid_t uid, const unsigned char header[12]);
int rme_vib_handle_next(uint64_t, struct rme_vib_step_event *);
int rme_vib_handle_complete(uint64_t, uint64_t event, int callback_ok);
int rme_vib_handle_stop(uint64_t);
int rme_vib_handle_destroy(uint64_t);
#endif
