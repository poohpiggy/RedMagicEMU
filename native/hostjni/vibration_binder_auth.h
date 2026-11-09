#ifndef RME_VIBRATION_BINDER_AUTH_H
#define RME_VIBRATION_BINDER_AUTH_H
#include <sys/types.h>

/* Owns duplicates only. Failure never authorizes a fallback connection. */
struct rme_binder_auth { int socket_fd, pidfd; pid_t peer_pid; uid_t peer_uid; };
int rme_binder_auth_open(struct rme_binder_auth *, int socket_fd, int pidfd, pid_t expected_pid, uid_t expected_uid);
int rme_binder_auth_alive(const struct rme_binder_auth *);
void rme_binder_auth_close(struct rme_binder_auth *);
#endif
