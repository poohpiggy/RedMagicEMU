#define _GNU_SOURCE
#include "ringtone_private_volume.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DEPTH 64
#define MAX_ENTRIES 100000

static int fail(int value) { errno = value; return -1; }
static int valid_id(const char *s) {
    static const char allowed[] = "abcdefghijkmnpqrstuvwxyz23456789";
    if (!s || strlen(s) != 6) return 0;
    for (int i = 0; i < 6; i++) if (!strchr(allowed, s[i])) return 0;
    return 1;
}
static int same(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}
static int owned_dir(const struct stat *s) { return S_ISDIR(s->st_mode) && s->st_uid == geteuid(); }

static int instance_check(int images, int instance, const char *id, struct stat *identity) {
    struct stat base, held, named;
    if (!valid_id(id)) return fail(EINVAL);
    if (fstat(images, &base) || fstat(instance, &held) ||
        fstatat(images, id, &named, AT_SYMLINK_NOFOLLOW)) return -1;
    if (!owned_dir(&base) || !owned_dir(&held) || !owned_dir(&named) || !same(&held, &named))
        return fail(ESTALE);
    if (identity) *identity = held;
    return 0;
}

void rme_ringtone_volume_close(struct rme_ringtone_volume *v) {
    if (!v) return;
    if (v->volume_fd >= 0) close(v->volume_fd);
    if (v->instance_fd >= 0) close(v->instance_fd);
    if (v->images_fd >= 0) close(v->images_fd);
    *v = (struct rme_ringtone_volume)RME_RINGTONE_VOLUME_INIT;
}

int rme_ringtone_volume_check(const struct rme_ringtone_volume *v, int lease) {
    struct stat instance, held, named;
    if (!v || lease != 1 || v->removed || v->images_fd < 0 || v->instance_fd < 0 || v->volume_fd < 0)
        return fail(EPERM);
    if (instance_check(v->images_fd, v->instance_fd, v->id, &instance)) return -1;
    if (instance.st_dev != v->instance_dev || instance.st_ino != v->instance_ino) return fail(ESTALE);
    if (fstat(v->volume_fd, &held) || fstatat(v->instance_fd, RME_RINGTONE_VOLUME_NAME,
                                               &named, AT_SYMLINK_NOFOLLOW)) return -1;
    if (!owned_dir(&held) || !owned_dir(&named) || !same(&held, &named) ||
        held.st_dev != v->volume_dev || held.st_ino != v->volume_ino ||
        held.st_dev != instance.st_dev) return fail(ESTALE);
    return 0;
}

int rme_ringtone_volume_create(int images_fd, int instance_fd, const char *id, int lease,
                               struct rme_ringtone_volume *out) {
    struct stat instance, volume;
    struct rme_ringtone_volume v = RME_RINGTONE_VOLUME_INIT;
    if (!out || lease != 1) return fail(EPERM);
    if (out->images_fd >= 0 || out->instance_fd >= 0 || out->volume_fd >= 0) return fail(EBUSY);
    if (instance_check(images_fd, instance_fd, id, &instance)) return -1;
    v.images_fd = fcntl(images_fd, F_DUPFD_CLOEXEC, 0);
    if (v.images_fd < 0) return -1;
    v.instance_fd = fcntl(instance_fd, F_DUPFD_CLOEXEC, 0);
    if (v.instance_fd < 0) goto error;
    v.instance_dev = instance.st_dev; v.instance_ino = instance.st_ino;
    memcpy(v.id, id, 7);
    if (mkdirat(v.instance_fd, RME_RINGTONE_VOLUME_NAME, 0700)) goto error;
    v.volume_fd = openat(v.instance_fd, RME_RINGTONE_VOLUME_NAME,
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (v.volume_fd < 0) goto error; /* Preserve the created directory on any failure. */
    if (fstat(v.volume_fd, &volume)) goto error;
    v.volume_dev = volume.st_dev; v.volume_ino = volume.st_ino;
    if (rme_ringtone_volume_check(&v, lease) || fsync(v.instance_fd) ||
        rme_ringtone_volume_check(&v, lease)) goto error;
    *out = v;
    return 0;
error: {
        int saved = errno;
        rme_ringtone_volume_close(&v);
        errno = saved;
        return -1;
    }
}

/* Open a fresh description for each directory walk (dup shares its offset). */
static int walk(int parent, dev_t device, unsigned depth, unsigned *count, int remove_entries) {
    if (depth > MAX_DEPTH) return fail(ELOOP);
    int fd = openat(parent, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    DIR *dir = fdopendir(fd);
    if (!dir) { int e = errno; close(fd); errno = e; return -1; }
    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) result = -1; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (++*count > MAX_ENTRIES) { result = fail(E2BIG); break; }
        struct stat named;
        if (fstatat(fd, entry->d_name, &named, AT_SYMLINK_NOFOLLOW)) { result = -1; break; }
        if (named.st_dev != device || named.st_uid != geteuid()) { result = fail(EXDEV); break; }
        if (S_ISLNK(named.st_mode)) { result = fail(ELOOP); break; }
        if (S_ISDIR(named.st_mode)) {
            int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat opened;
            if (child < 0) { result = -1; break; }
            if (fstat(child, &opened) || !same(&named, &opened)) {
                int e = errno ? errno : ESTALE; close(child); errno = e; result = -1; break;
            }
            result = walk(child, device, depth + 1, count, remove_entries);
            if (close(child) && !result) result = -1;
            if (result) break;
            if (remove_entries) {
                struct stat now;
                if (fstatat(fd, entry->d_name, &now, AT_SYMLINK_NOFOLLOW) || !same(&named, &now)) {
                    if (!errno) errno = ESTALE;
                    result = -1; break;
                }
                if (unlinkat(fd, entry->d_name, AT_REMOVEDIR)) { result = -1; break; }
            }
        } else if (S_ISREG(named.st_mode) && named.st_nlink == 1) {
            if (remove_entries) {
                struct stat now;
                if (fstatat(fd, entry->d_name, &now, AT_SYMLINK_NOFOLLOW) || !same(&named, &now) ||
                    !S_ISREG(now.st_mode) || now.st_nlink != 1) {
                    if (!errno) errno = ESTALE;
                    result = -1; break;
                }
                if (unlinkat(fd, entry->d_name, 0)) { result = -1; break; }
            }
        } else { result = fail(EINVAL); break; }
    }
    int saved = errno;
    if (closedir(dir) && !result) return -1;
    errno = saved;
    return result;
}

int rme_ringtone_volume_remove(struct rme_ringtone_volume *v, int lease, int writers_exited) {
    if (writers_exited != 1) return fail(EPERM);
    if (rme_ringtone_volume_check(v, lease)) return -1;
    unsigned count = 0;
    if (walk(v->volume_fd, v->volume_dev, 0, &count, 0)) return -1;
    if (rme_ringtone_volume_check(v, lease)) return -1;
    count = 0;
    if (walk(v->volume_fd, v->volume_dev, 0, &count, 1)) return -1;
    if (rme_ringtone_volume_check(v, lease)) return -1;
    if (unlinkat(v->instance_fd, RME_RINGTONE_VOLUME_NAME, AT_REMOVEDIR)) return -1;
    v->removed = 1; /* Even fsync failure cannot make this safe to reuse/retry. */
    return fsync(v->instance_fd);
}
