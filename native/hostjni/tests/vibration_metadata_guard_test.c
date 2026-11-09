#define _GNU_SOURCE
#include "../vibration_metadata_guard.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_fchmodat2
#define SYS_fchmodat2 452
#endif
#ifndef SYS_setxattrat
#define SYS_setxattrat 463
#endif
#ifndef SYS_removexattrat
#define SYS_removexattrat 466
#endif

#define DENIED(expr) do { errno = 0; if ((expr) != -1 || errno != EPERM) _exit(31); } while (0)

int main(void) {
    char name[] = "/tmp/rme-vib-metadata-XXXXXX";
    int fd = mkstemp(name);
    assert(fd >= 0);
    assert(fchmod(fd, 0600) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        if (rme_vibration_metadata_restrict() != 0) _exit(30);
        /* Read and ordinary file output remain usable; a pre-opened FD does
         * not bypass restrictions on its metadata operations. */
        if (write(fd, "x", 1) != 1) _exit(32);
        char value;
        if (pread(fd, &value, 1, 0) != 1 || value != 'x') _exit(34);
        struct stat st;
        if (fstat(fd, &st) != 0) _exit(33);
        DENIED(fchmod(fd, 0644));
        DENIED(chmod(name, 0644));
        DENIED(fchmodat(AT_FDCWD, name, 0644, 0));
        DENIED(syscall(SYS_fchmodat2, AT_FDCWD, name, 0644, 0));
        DENIED(fchown(fd, getuid(), getgid()));
        DENIED(fchownat(AT_FDCWD, name, getuid(), getgid(), 0));
        DENIED(utimensat(AT_FDCWD, name, NULL, 0));
        DENIED(setxattr(name, "user.rme", "x", 1, 0));
        DENIED(fsetxattr(fd, "user.rme", "x", 1, 0));
        DENIED(removexattr(name, "user.rme"));
        DENIED(fremovexattr(fd, "user.rme"));
        /* These newer at-style calls must be denied before dereferencing the
         * null path; the seccomp rule must not depend on host kernel support. */
        DENIED(syscall(SYS_setxattrat, AT_FDCWD, NULL, 0, NULL, 0));
        DENIED(syscall(SYS_removexattrat, AT_FDCWD, NULL, 0, NULL));
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "metadata guard child failed: status=%d\n", status);
        close(fd); unlink(name); return 1;
    }
    struct stat st;
    assert(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(close(fd) == 0 && unlink(name) == 0);
    puts("VIBRATION_METADATA_GUARD_OK");
    return 0;
}
