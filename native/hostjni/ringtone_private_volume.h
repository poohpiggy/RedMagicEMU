#ifndef RME_RINGTONE_PRIVATE_VOLUME_H
#define RME_RINGTONE_PRIVATE_VOLUME_H

#include <sys/types.h>

/* Private, fixed name; a pre-existing entry is never reused. */
#define RME_RINGTONE_VOLUME_NAME ".rme-ringtone-init-volume"

/* The caller owns the images and instance descriptors and must already have
 * verified they belong to the app's private instance tree. Every caller that
 * can rename/delete the instance must cooperate with the same exclusive
 * runtime lease. The integer flags below are explicit assertions by that
 * caller, not proof that a lease is held or a process tree has stopped. */
struct rme_ringtone_volume {
    int images_fd, instance_fd, volume_fd;
    dev_t instance_dev, volume_dev;
    ino_t instance_ino, volume_ino;
    char id[7];
    int removed;
};
#define RME_RINGTONE_VOLUME_INIT { .images_fd = -1, .instance_fd = -1, .volume_fd = -1 }

/* mkdirat is exclusive; failure after creation leaves the entry in place. */
int rme_ringtone_volume_create(int images_fd, int instance_fd, const char *id,
                               int exclusive_lease_held, struct rme_ringtone_volume *out);
/* Re-check descriptors and their current anchored names. Does not eliminate
 * races with code that ignores the caller's lease after this check. */
int rme_ringtone_volume_check(const struct rme_ringtone_volume *v, int exclusive_lease_held);
/* Caller must independently prove all Guest/provider writers have exited.
 * A preflight rejects links, mounts and unexpected file types before deletion.
 * Later I/O failures may leave a partially cleaned private volume: never treat
 * an error as successful cleanup or permission to publish/reuse the instance. */
int rme_ringtone_volume_remove(struct rme_ringtone_volume *v, int exclusive_lease_held,
                               int all_guest_writers_confirmed_exited);
void rme_ringtone_volume_close(struct rme_ringtone_volume *v);

#endif
