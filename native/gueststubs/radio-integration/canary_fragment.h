#ifndef RME_CANARY_FRAGMENT_H
#define RME_CANARY_FRAGMENT_H

/* Manual, single-use diagnostic only. All fds must be verified, pinned directories:
 * stagefd outside all VINTF scanning; manifestfd the intended Guest fragment directory.
 * The caller must independently exclude all competing same-UID writers and ensure
 * every Guest process is stopped throughout publication and cleanup. The guard is
 * only a caller assertion, not a lock. Never use this as a production recovery gate.
 * Error paths intentionally preserve any files already created for manual review. */
typedef int (*rme_canary_guard)(void *context);
int rme_canary_publish(int stagefd, int manifestfd, const char *token, rme_canary_guard guard, void *context);
int rme_canary_verify(int stagefd, int manifestfd, const char *token);
int rme_canary_cleanup(int stagefd, int manifestfd, const char *token, rme_canary_guard guard, void *context);
#endif
