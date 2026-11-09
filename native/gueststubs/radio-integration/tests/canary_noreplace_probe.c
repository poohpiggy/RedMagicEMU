#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

/* Only accepts a NEW path in caller-provided app-private cache; never touches a Guest tree. */
int main(int argc, char **argv) {
    if (argc != 2 || strstr(argv[1], "..") || !strstr(argv[1], "/cache/rme-canary-probe-")) return 2;
    if (mkdir(argv[1], 0700)) { perror("mkdir unique probe"); return 3; }
    int base = open(argv[1], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    assert(base >= 0);
    assert(!mkdirat(base, "staging", 0700)); assert(!mkdirat(base, "manifest", 0700));
    int staging = openat(base, "staging", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int manifest = openat(base, "manifest", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    assert(staging >= 0 && manifest >= 0);
    struct stat a, b; assert(!fstat(staging, &a) && !fstat(manifest, &b) && a.st_dev == b.st_dev);
    assert(!fsync(base)); assert(!fsync(staging)); assert(!fsync(manifest));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        int fd = openat(staging, "stage", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 || write(fd, "canary", 6) != 6 || fsync(fd) || close(fd) || fsync(staging)) _exit(8);
        _exit(0); /* crash boundary: fully staged, nothing published */
    }
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    errno = 0; assert(fstatat(manifest, "test.xml", &b, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    assert(!fstatat(staging, "stage", &a, AT_SYMLINK_NOFOLLOW) && S_ISREG(a.st_mode) && a.st_size == 6);
    int target = openat(manifest, "test.xml", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    assert(target >= 0 && write(target, "existing", 8) == 8 && !fsync(target) && !close(target));
    assert(!fsync(manifest));
    errno = 0;
    long ret = syscall(__NR_renameat2, staging, "stage", manifest, "test.xml", RENAME_NOREPLACE);
    int err = errno;
    printf("COLLISION ret=%ld errno=%d\n", ret, err); fflush(stdout);
    assert(ret == -1 && err == EEXIST);
    target = openat(manifest, "test.xml", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    char data[16] = {0}; assert(target >= 0 && read(target, data, sizeof data) == 8 && !memcmp(data, "existing", 8)); assert(!close(target));
    assert(!unlinkat(manifest, "test.xml", 0)); assert(!fsync(manifest));
    errno = 0; ret = syscall(__NR_renameat2, staging, "stage", manifest, "test.xml", RENAME_NOREPLACE); err = errno;
    printf("PUBLISH ret=%ld errno=%d\n", ret, err); fflush(stdout); assert(ret == 0);
    assert(!fsync(staging) && !fsync(manifest));
    assert(!fstatat(manifest, "test.xml", &b, AT_SYMLINK_NOFOLLOW));
    assert(b.st_dev == a.st_dev && b.st_ino == a.st_ino && b.st_size == 6);
    errno = 0; assert(fstatat(staging, "stage", &b, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    assert(!unlinkat(manifest, "test.xml", 0)); assert(!fsync(manifest));
    assert(!close(staging) && !close(manifest));
    assert(!unlinkat(base, "staging", AT_REMOVEDIR)); assert(!unlinkat(base, "manifest", AT_REMOVEDIR));
    assert(!fsync(base) && !close(base) && !rmdir(argv[1]));
    puts("CANARY_NOREPLACE_PROBE_OK (power-loss durability not tested)"); return 0;
}
