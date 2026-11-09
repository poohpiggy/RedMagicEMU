#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern int test_reap_owned_children(void);

/* Test-only: dirfd must already identify a private, exclusively held directory.
 * No protection from malicious same-UID concurrent replacement. */
int test_supervisor_mark_dirty(int dirfd) {
    int fd = openat(dirfd, "dirty", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (write(fd, "dirty\n", 6) != 6 || fsync(fd) != 0 || fsync(dirfd) != 0) {
        close(fd); /* A partly written marker is deliberately retained. */
        return -1;
    }
    return fd;
}

int test_supervisor_finish(int dirfd, int marker_fd, int fail_scan) {
    if (fail_scan || test_reap_owned_children() != 0) return -1;
    struct stat held, path;
    if (fstat(marker_fd, &held) != 0 || fstatat(dirfd, "dirty", &path, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(held.st_mode) || !S_ISREG(path.st_mode) || held.st_nlink != 1 ||
        held.st_dev != path.st_dev || held.st_ino != path.st_ino) return -1;
    if (unlinkat(dirfd, "dirty", 0) != 0) return -1;
    return fsync(dirfd) == 0 ? 0 : -1;
}
