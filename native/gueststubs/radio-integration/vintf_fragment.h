#ifndef RME_RADIO_VINTF_FRAGMENT_H
#define RME_RADIO_VINTF_FRAGMENT_H

/* Isolated experiment only; not wired into normal Guest startup. */
#define RME_RADIO_VINTF_NAME "aemu-radio-slot1.xml"

/*
 * dirfd is an already verified, open vendor/etc/vintf/manifest directory in
 * the admitted experiment instance. The caller must hold the instance-wide
 * exclusive lifecycle gate and keep every Guest process stopped throughout
 * prepare/cleanup. guard rechecks that caller-owned condition immediately
 * before mutation; it is NOT an OS-enforced lock or protection against
 * malicious same-UID path mutation. Never use without that external gate.
 * token must be a fresh 32-character lowercase hexadecimal session ID.
 *
 * prepare leaves a synced staged file and an identity record alongside an
 * atomically created relative symlink. A successful result only means the file was published; it does
 * not mean services were started or ready. The caller must start/verify all
 * declared services, stop the Guest on failure, then invoke cleanup.
 * verify requires the current seven-service content. cleanup also accepts the
 * exact earlier six-service content, after the same identity and guard checks;
 * it refuses conflicting files and supports interruption during its own cleanup.
 * VINTF symlink loading on the target has not been verified; do
 * not use on any real Guest until that and the external gate are verified.
 */
typedef int (*rme_radio_stopped_guard)(void *context);
int rme_radio_vintf_prepare(int dirfd, const char *token, rme_radio_stopped_guard guard, void *context);
int rme_radio_vintf_verify(int dirfd, const char *token);
int rme_radio_vintf_cleanup(int dirfd, const char *token, rme_radio_stopped_guard guard, void *context);

#endif
