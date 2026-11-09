#define _GNU_SOURCE
#include "vintf_fragment.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* With an empty second substitution this is the frozen six-service format.
 * Any future format change must retain those exact bytes for cleanup. */
static const char fragment_format[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- RedMagicEMU isolated radio experiment %s -->\n"
    "<manifest version=\"1.0\" type=\"device\">\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.config</name><version>4</version><fqname>IRadioConfig/default</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.modem</name><version>4</version><fqname>IRadioModem/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.sim</name><version>4</version><fqname>IRadioSim/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.network</name><version>4</version><fqname>IRadioNetwork/slot1</fqname></hal>\n"
    "%s"
    " <hal format=\"aidl\"><name>android.hardware.radio.messaging</name><version>4</version><fqname>IRadioMessaging/slot1</fqname></hal>\n"
    " <hal format=\"aidl\"><name>android.hardware.radio.voice</name><version>4</version><fqname>IRadioVoice/slot1</fqname></hal>\n"
    "</manifest>\n";

static const char data_declaration[] =
    " <hal format=\"aidl\"><name>android.hardware.radio.data</name><version>4</version><fqname>IRadioData/slot1</fqname></hal>\n";

static int inputs(int dirfd, const char *token, char *name, size_t name_size,
                  char *xml, size_t xml_size, size_t *xml_len, int legacy) {
    struct stat st;
    if (fstat(dirfd, &st) || !S_ISDIR(st.st_mode) || !token || strlen(token) != 32) { errno=EINVAL; return -1; }
    for (int i=0;i<32;i++) if (!((token[i]>='0' && token[i]<='9') || (token[i]>='a' && token[i]<='f'))) { errno=EINVAL; return -1; }
    int n=snprintf(name,name_size,".aemu-radio-slot1-%s.data",token);
    int m=snprintf(xml,xml_size,fragment_format,token,legacy ? "" : data_declaration);
    if (n<0 || (size_t)n>=name_size || m<0 || (size_t)m>=xml_size) { errno=EOVERFLOW; return -1; }
    *xml_len=(size_t)m; return 0;
}

static int check_file(int dirfd, const char *name, const char *xml, size_t n, struct stat *st) {
    int fd=openat(dirfd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) return -1;
    int rc=-1; char buf[2048];
    if (!fstat(fd,st) && S_ISREG(st->st_mode) && st->st_size==(off_t)n && n<=sizeof buf) {
        size_t at=0;
        while (at<n) { ssize_t r=read(fd,buf+at,n-at); if (r<=0) break; at+=(size_t)r; }
        if (at==n && !memcmp(buf,xml,n)) rc=0;
    }
    int saved=rc ? EINVAL : 0; close(fd); if (saved) errno=saved; return rc;
}

static int allow(rme_radio_stopped_guard guard, void *context) {
    if (!guard || guard(context)!=1) { errno=EBUSY; return 0; }
    return 1;
}

int rme_radio_vintf_prepare(int dirfd, const char *token, rme_radio_stopped_guard guard, void *context) {
    char name[96], meta[112], record[160], xml[2048]; size_t n; struct stat st, existing;
    if (inputs(dirfd,token,name,sizeof name,xml,sizeof xml,&n,0) || !allow(guard,context)) return -1;
    if (fstatat(dirfd,RME_RADIO_VINTF_NAME,&existing,AT_SYMLINK_NOFOLLOW)==0) { errno=EEXIST; return -1; }
    if (errno!=ENOENT) return -1;
    if (snprintf(meta,sizeof meta,"%s.meta",name)>=(int)sizeof meta) { errno=EOVERFLOW; return -1; }
    int fd=openat(dirfd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (fd<0) return -1;
    size_t at=0; int rc=-1, meta_created=0, published=0, meta_fd=-1;
    while (at<n) { ssize_t w=write(fd,xml+at,n-at); if (w<=0) goto out; at+=(size_t)w; }
    if (fsync(fd) || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1) goto out;
    int count=snprintf(record,sizeof record,"RME1 %s %" PRIuMAX " %" PRIuMAX "\n",token,(uintmax_t)st.st_dev,(uintmax_t)st.st_ino);
    if (count<0 || (size_t)count>=sizeof record) { errno=EOVERFLOW; goto out; }
    meta_fd=openat(dirfd,meta,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (meta_fd<0) goto out;
    meta_created=1;
    at=0;
    while (at<(size_t)count) { ssize_t w=write(meta_fd,record+at,(size_t)count-at); if (w<=0) goto out; at+=(size_t)w; }
    if (fsync(meta_fd) || fsync(dirfd) || !allow(guard,context)) goto out;
    if (symlinkat(name,dirfd,RME_RADIO_VINTF_NAME)) goto out;
    published=1;
    if (fsync(dirfd)) goto out;
    rc=0;
out: {
        int saved=errno; if (meta_fd>=0) close(meta_fd); close(fd);
        /* Do not remove anything if the stopped/exclusive precondition ceased. */
        if (rc && !published && allow(guard,context)) {
            if (meta_created) unlinkat(dirfd,meta,0);
            unlinkat(dirfd,name,0);
        }
        errno=saved; return rc;
    }
}

static int verify_owned(int dirfd, const char *token, char *name, size_t name_size,
                        char *meta, size_t meta_size, int allow_legacy) {
    char xml[2048], record[160]; size_t n; struct stat own, meta_stat;
    if (inputs(dirfd,token,name,name_size,xml,sizeof xml,&n,0)) { errno=EINVAL; return -1; }
    if (check_file(dirfd,name,xml,n,&own)) {
        /* Legacy bytes are recognized only for cleanup, never readiness. */
        if (!allow_legacy || inputs(dirfd,token,name,name_size,xml,sizeof xml,&n,1) ||
            check_file(dirfd,name,xml,n,&own)) { errno=EINVAL; return -1; }
    }
    if (own.st_nlink!=1) { errno=EINVAL; return -1; }
    if (snprintf(meta,meta_size,"%s.meta",name)>=(int)meta_size) { errno=EOVERFLOW; return -1; }
    int count=snprintf(record,sizeof record,"RME1 %s %" PRIuMAX " %" PRIuMAX "\n",token,(uintmax_t)own.st_dev,(uintmax_t)own.st_ino);
    if (count<0 || (size_t)count>=sizeof record || check_file(dirfd,meta,record,(size_t)count,&meta_stat) || meta_stat.st_nlink!=1) { errno=EINVAL; return -1; }
    return 0;
}

static int target_matches(int dirfd, const char *name, int *present) {
    struct stat target; char link[128];
    *present=fstatat(dirfd,RME_RADIO_VINTF_NAME,&target,AT_SYMLINK_NOFOLLOW)==0;
    if (!*present && errno!=ENOENT) return -1;
    if (*present) {
        if (!S_ISLNK(target.st_mode)) { errno=EINVAL; return -1; }
        ssize_t n=readlinkat(dirfd,RME_RADIO_VINTF_NAME,link,sizeof link);
        if (n<0 || (size_t)n!=strlen(name) || memcmp(link,name,(size_t)n)) { errno=EINVAL; return -1; }
    }
    return 0;
}

/* Validate a remaining journal after target and stage were both removed. */
static int orphan_meta_matches(int dirfd, const char *meta, const char *token) {
    int fd=openat(dirfd,meta,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) return -1;
    struct stat st; char data[160], parsed_token[33], canonical[160], end; uintmax_t dev,ino;
    ssize_t n=-1;
    if (!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>0 && st.st_size<(off_t)sizeof data)
        n=read(fd,data,(size_t)st.st_size);
    close(fd);
    if (n<=0 || n!=st.st_size) { errno=EINVAL; return -1; }
    data[n]='\0';
    if (sscanf(data,"RME1 %32[0-9a-f] %" SCNuMAX " %" SCNuMAX "%c",parsed_token,&dev,&ino,&end)!=4 ||
        strcmp(parsed_token,token) || end!='\n') { errno=EINVAL; return -1; }
    int len=snprintf(canonical,sizeof canonical,"RME1 %s %" PRIuMAX " %" PRIuMAX "\n",token,dev,ino);
    if (len!=n || memcmp(canonical,data,(size_t)n)) { errno=EINVAL; return -1; }
    return 0;
}

int rme_radio_vintf_verify(int dirfd, const char *token) {
    char name[96],meta[112]; int present;
    if (verify_owned(dirfd,token,name,sizeof name,meta,sizeof meta,0) || target_matches(dirfd,name,&present) || !present) { errno=EINVAL; return -1; }
    return 0;
}

int rme_radio_vintf_cleanup(int dirfd, const char *token, rme_radio_stopped_guard guard, void *context) {
    char name[96],meta[112],xml[2048]; size_t n; int present; struct stat st;
    if (inputs(dirfd,token,name,sizeof name,xml,sizeof xml,&n,0) || !allow(guard,context)) return -1;
    if (snprintf(meta,sizeof meta,"%s.meta",name)>=(int)sizeof meta) { errno=EOVERFLOW; return -1; }
    if (fstatat(dirfd,name,&st,AT_SYMLINK_NOFOLLOW)) {
        if (errno!=ENOENT) return -1;
        if (fstatat(dirfd,RME_RADIO_VINTF_NAME,&st,AT_SYMLINK_NOFOLLOW)==0 || errno!=ENOENT) { errno=EINVAL; return -1; }
        if (fstatat(dirfd,meta,&st,AT_SYMLINK_NOFOLLOW)) return errno==ENOENT ? 0 : -1;
        if (orphan_meta_matches(dirfd,meta,token) || !allow(guard,context) || unlinkat(dirfd,meta,0)) return -1;
        return fsync(dirfd);
    }
    if (verify_owned(dirfd,token,name,sizeof name,meta,sizeof meta,1) || target_matches(dirfd,name,&present)) return -1;
    if (!allow(guard,context)) return -1;
    if (present && unlinkat(dirfd,RME_RADIO_VINTF_NAME,0)) return -1;
    if (unlinkat(dirfd,name,0)) return -1;
    if (unlinkat(dirfd,meta,0)) return -1;
    return fsync(dirfd);
}
