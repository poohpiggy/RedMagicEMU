#define _GNU_SOURCE
#include "../ringtone_private_volume.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Only touches a unique directory created by this test under TMPDIR. */
static void write_at(int d, const char *name, const char *text) {
    int fd = openat(d, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    assert(close(fd) == 0);
}

static int exists_at(int d, const char *name) {
    struct stat st;
    return fstatat(d, name, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

int main(void) {
    char template[PATH_MAX];
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    int length = snprintf(template, sizeof template, "%s/rme-private-volume-XXXXXX", base);
    assert(length > 0 && (size_t)length < sizeof template);
    char *temp = mkdtemp(template); assert(temp);
    int root = open(temp, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(root >= 0);
    assert(mkdirat(root, "images", 0700) == 0);
    int images = openat(root, "images", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(images >= 0);
    assert(mkdirat(images, "abcdef", 0700) == 0);
    int instance = openat(images, "abcdef", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(instance >= 0);
    write_at(root, "outside", "keep");
    struct rme_ringtone_volume v = RME_RINGTONE_VOLUME_INIT;
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 0, &v) < 0);
    assert(!exists_at(instance, RME_RINGTONE_VOLUME_NAME));
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &v) == 0);
    assert(rme_ringtone_volume_check(&v, 1) == 0);
    assert(rme_ringtone_volume_check(&v, -1) < 0);
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &v) < 0 && errno == EBUSY);
    struct rme_ringtone_volume duplicate = RME_RINGTONE_VOLUME_INIT;
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &duplicate) < 0 && errno == EEXIST);
    assert(rme_ringtone_volume_remove(&v, 1, 0) < 0);
    assert(rme_ringtone_volume_remove(&v, 1, -1) < 0);
    assert(exists_at(instance, RME_RINGTONE_VOLUME_NAME));

    assert(mkdirat(v.volume_fd, "nested", 0700) == 0);
    int nested = openat(v.volume_fd, "nested", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(nested >= 0);
    write_at(nested, "owned", "safe");
    assert(symlinkat("../../../outside", nested, "link") == 0);
    assert(rme_ringtone_volume_remove(&v, 1, 1) < 0);
    assert(exists_at(root, "outside") && exists_at(nested, "owned") && exists_at(nested, "link"));
    assert(unlinkat(nested, "link", 0) == 0);
    if (linkat(root, "outside", nested, "hardlink", 0) == 0) {
        assert(rme_ringtone_volume_remove(&v, 1, 1) < 0);
        assert(exists_at(root, "outside") && exists_at(nested, "owned") && exists_at(nested, "hardlink"));
        assert(unlinkat(nested, "hardlink", 0) == 0);
        puts("HARDLINK_GUARD_PASSED");
    } else {
        int reason = errno;
        /* Some Android filesystems/security policies prohibit all hardlinks.
         * Only explicit unsupported/denied errors skip this one case. */
        assert(reason == EPERM || reason == EACCES || reason == EOPNOTSUPP || reason == ENOSYS);
        printf("HARDLINK_TEST_SKIPPED_ERRNO=%d\n", reason);
    }
    assert(rme_ringtone_volume_remove(&v, 1, 1) == 0);
    assert(!exists_at(instance, RME_RINGTONE_VOLUME_NAME));
    assert(exists_at(root, "outside"));
    assert(close(nested) == 0);
    rme_ringtone_volume_close(&v);

    /* A pre-existing volume, however named, is never reused or removed. */
    assert(mkdirat(instance, RME_RINGTONE_VOLUME_NAME, 0700) == 0);
    int existing = openat(instance, RME_RINGTONE_VOLUME_NAME, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(existing >= 0);
    write_at(existing, "user", "keep");
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &v) < 0 && errno == EEXIST);
    assert(exists_at(existing, "user"));
    assert(unlinkat(existing, "user", 0) == 0); assert(close(existing) == 0);
    assert(unlinkat(instance, RME_RINGTONE_VOLUME_NAME, AT_REMOVEDIR) == 0);

    /* Replacing only the volume path is also rejected without deleting either tree. */
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &v) == 0);
    assert(renameat(instance, RME_RINGTONE_VOLUME_NAME, instance, ".old-volume") == 0);
    assert(mkdirat(instance, RME_RINGTONE_VOLUME_NAME, 0700) == 0);
    int fake = openat(instance, RME_RINGTONE_VOLUME_NAME, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(fake >= 0);
    write_at(fake, "user", "keep");
    assert(rme_ringtone_volume_check(&v, 1) < 0);
    assert(rme_ringtone_volume_remove(&v, 1, 1) < 0);
    assert(exists_at(fake, "user") && exists_at(instance, ".old-volume"));
    assert(unlinkat(fake, "user", 0) == 0); assert(close(fake) == 0);
    assert(unlinkat(instance, RME_RINGTONE_VOLUME_NAME, AT_REMOVEDIR) == 0);
    assert(renameat(instance, ".old-volume", instance, RME_RINGTONE_VOLUME_NAME) == 0);
    assert(rme_ringtone_volume_remove(&v, 1, 1) == 0);
    rme_ringtone_volume_close(&v);

    /* Replacing the instance path invalidates the old descriptor without touching the replacement. */
    assert(rme_ringtone_volume_create(images, instance, "abcdef", 1, &v) == 0);
    assert(renameat(images, "abcdef", images, "bcdefg") == 0);
    assert(mkdirat(images, "abcdef", 0700) == 0);
    int replacement = openat(images, "abcdef", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); assert(replacement >= 0);
    write_at(replacement, "user", "keep");
    assert(rme_ringtone_volume_check(&v, 1) < 0);
    assert(rme_ringtone_volume_remove(&v, 1, 1) < 0);
    assert(exists_at(replacement, "user") && exists_at(instance, RME_RINGTONE_VOLUME_NAME));
    assert(unlinkat(replacement, "user", 0) == 0); assert(close(replacement) == 0);
    assert(unlinkat(images, "abcdef", AT_REMOVEDIR) == 0);
    assert(renameat(images, "bcdefg", images, "abcdef") == 0);
    assert(rme_ringtone_volume_remove(&v, 1, 1) == 0);
    rme_ringtone_volume_close(&v);

    assert(symlinkat("../outside", images, "bcdefg") == 0);
    assert(rme_ringtone_volume_create(images, instance, "bcdefg", 1, &v) < 0);
    assert(exists_at(root, "outside")); assert(unlinkat(images, "bcdefg", 0) == 0);
    assert(close(instance) == 0); assert(unlinkat(images, "abcdef", AT_REMOVEDIR) == 0);
    assert(close(images) == 0); assert(unlinkat(root, "images", AT_REMOVEDIR) == 0);
    assert(unlinkat(root, "outside", 0) == 0); assert(close(root) == 0); assert(rmdir(temp) == 0);
    puts("RINGTONE_PRIVATE_VOLUME_OK");
    return 0;
}
