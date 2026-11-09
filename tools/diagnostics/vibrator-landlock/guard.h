#ifndef RME_VIBRATOR_LANDLOCK_GUARD_H
#define RME_VIBRATOR_LANDLOCK_GUARD_H

/* Research only. Calling this irreversibly restricts the current thread and
 * descendants. guest_dirfd must refer to an independently verified directory.
 * This does not sanitize already-open FDs or cover every filesystem operation. */
int rme_vib_landlock(int guest_dirfd, unsigned required_abi);

/* Research only: call from a single-threaded child before restriction.
 * Keep only standard descriptors and the Guest directory descriptor. The
 * caller must establish that those retained descriptors are trustworthy and
 * must abort the child if this function returns an error; it is irreversible. */
int rme_vib_close_unneeded_fds(int guest_dirfd);
#endif
