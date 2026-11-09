#ifndef RME_VIBRATION_PUBLISH_H
#define RME_VIBRATION_PUBLISH_H
/* 0 on success, otherwise negative errno. The caller owns directory_fd.
 * The directory must not be concurrently modified by an untrusted process:
 * renameat2 does not atomically pin a source filename to an opened inode. */
int rme_vib_publish_noreplace(int directory_fd, const char *stage, const char *target);
#endif
