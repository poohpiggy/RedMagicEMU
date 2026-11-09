#ifndef RME_VIBRATION_METADATA_GUARD_H
#define RME_VIBRATION_METADATA_GUARD_H

/* Experimental only. Restricts the calling thread and its future children;
 * it does not establish file, device, ioctl, FD, or application isolation.
 * Call in a fresh single-threaded process before running Guest code. Failure
 * must terminate that process. The filter cannot be removed after success. */
int rme_vibration_metadata_restrict(void);

#endif
