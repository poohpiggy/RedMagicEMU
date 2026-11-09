#ifndef RME_VIBRATION_BPROTO_CLIENT_H
#define RME_VIBRATION_BPROTO_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Offline-only transport: not a service registration or authorization. */
struct rme_bproto_client { int fd; unsigned guest_uid; };
struct d_hdr;

/* Takes ownership of fd even on failure; checks the connected daemon's credentials. */
int rme_bproto_open(struct rme_bproto_client *, int fd, pid_t peer_pid, uid_t peer_uid,
                    unsigned guest_uid, unsigned process_key);
int rme_bproto_txn(struct rme_bproto_client *, unsigned handle, unsigned code, unsigned flags,
                   const void *, size_t, const uint64_t *, size_t);
int rme_bproto_reply(struct rme_bproto_client *, const void *, size_t);
int rme_bproto_read_request(struct rme_bproto_client *, unsigned read_size);
int rme_bproto_receive(struct rme_bproto_client *, struct d_hdr *, void *, size_t, size_t *);
void rme_bproto_close(struct rme_bproto_client *);
#endif
