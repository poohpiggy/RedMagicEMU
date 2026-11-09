#define _GNU_SOURCE
#include "vibration_publish.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif

static int safe_name(const char *s) {
    if (!s || !*s || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    size_t i=0;
    for (;s[i];++i) {
        unsigned char c=(unsigned char)s[i];
        if (i>=255 || !((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
                        (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    return 1;
}
static int same_inode(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && S_ISREG(a->st_mode) && S_ISREG(b->st_mode);
}
int rme_vib_publish_noreplace(int directory_fd,const char *stage,const char *target) {
    if (directory_fd<0 || !safe_name(stage) || !safe_name(target) || !strcmp(stage,target)) return -EINVAL;
    int dir=fcntl(directory_fd,F_DUPFD_CLOEXEC,0);
    if (dir<0) return -errno;
    int result=0, file=-1; struct stat dst, staged, named, after;
    if (fstat(dir,&dst)<0) { result=-errno; goto out; }
    if (!S_ISDIR(dst.st_mode)) { result=-ENOTDIR; goto out; }
    /* O_PATH inspects the inode without opening a possible device or FIFO. */
    file=openat(dir,stage,O_PATH|O_CLOEXEC|O_NOFOLLOW);
    if (file<0) { result=-errno; goto out; }
    if (fstat(file,&staged)<0 || fstatat(dir,stage,&named,AT_SYMLINK_NOFOLLOW)<0) { result=-errno; goto out; }
    if (!same_inode(&staged,&named)) { result=-EINVAL; goto out; }
#ifdef SYS_renameat2
    if (syscall(SYS_renameat2,dir,stage,dir,target,RENAME_NOREPLACE)<0) { result=-errno; goto out; }
#else
    result=-ENOSYS; goto out;
#endif
    /* Do not remove an unexpected target: it might belong to another actor. */
    if (fstatat(dir,target,&after,AT_SYMLINK_NOFOLLOW)<0 || !same_inode(&staged,&after)) result=-ESTALE;
out:
    if (file>=0) close(file);
    close(dir);
    return result;
}
