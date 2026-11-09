#define _GNU_SOURCE
#include "guard.h"
#include <errno.h>
#include <linux/landlock.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_ACCESS_FS_IOCTL_DEV (1ULL << 15)
#endif

int rme_vib_close_unneeded_fds(int guest_dirfd) {
    if (guest_dirfd < 0) return -EBADF;
    if (guest_dirfd < 3) return -EINVAL;
    struct stat st;
    if (fstat(guest_dirfd, &st) < 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
#ifdef SYS_close_range
    if (guest_dirfd > 3 && syscall(SYS_close_range, 3U, (unsigned)guest_dirfd - 1, 0U) < 0)
        return -errno;
    if (syscall(SYS_close_range, (unsigned)guest_dirfd + 1, UINT32_MAX, 0U) < 0)
        return -errno;
    return 0;
#else
    return -ENOSYS;
#endif
}

int rme_vib_landlock(int guest_dirfd, unsigned required_abi) {
    if (guest_dirfd < 0) return -EBADF;
    struct stat st;
    if (fstat(guest_dirfd, &st) < 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0) return -errno;
    if (abi < 5 || (unsigned long)abi < required_abi) return -EOPNOTSUPP;
    /* All filesystem rights defined through ABI 5, including IOCTL_DEV. */
    const uint64_t rights = (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1;
    struct landlock_ruleset_attr rules = { .handled_access_fs = rights };
    int fd = (int)syscall(SYS_landlock_create_ruleset, &rules, sizeof rules, 0);
    if (fd < 0) return -errno;
    struct landlock_path_beneath_attr allowed = { .allowed_access = rights, .parent_fd = guest_dirfd };
    int result = 0;
    if (syscall(SYS_landlock_add_rule, fd, LANDLOCK_RULE_PATH_BENEATH, &allowed, 0) < 0) result = -errno;
    else if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) result = -errno;
    else if (syscall(SYS_landlock_restrict_self, fd, 0) < 0) result = -errno;
    close(fd);
    return result;
}
