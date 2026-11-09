#define _GNU_SOURCE
#include "canary_fragment.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

struct names { char temp[80], meta[80], target[80], xml[512]; size_t len; };
static int names_for(const char *token, struct names *n) {
    if (!token || strlen(token)!=32) { errno=EINVAL; return -1; }
    for (int i=0;i<32;i++) if (!((token[i]>='0'&&token[i]<='9') || (token[i]>='a'&&token[i]<='f'))) { errno=EINVAL; return -1; }
    if (snprintf(n->temp,sizeof n->temp,".rme-canary-%s.stage",token)>=(int)sizeof n->temp ||
        snprintf(n->meta,sizeof n->meta,".rme-canary-%s.meta",token)>=(int)sizeof n->meta ||
        snprintf(n->target,sizeof n->target,"aemu-canary-%s.xml",token)>=(int)sizeof n->target) { errno=EOVERFLOW; return -1; }
    int k=snprintf(n->xml,sizeof n->xml,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!-- RedMagicEMU canary %s -->\n<manifest version=\"1.0\" type=\"device\">\n"
        " <hal format=\"aidl\"><name>android.hardware.radio.config</name><version>4</version>"
        "<fqname>IRadioConfig/rme%s</fqname></hal>\n</manifest>\n",token,token);
    if (k<0 || (size_t)k>=sizeof n->xml) { errno=EOVERFLOW; return -1; }
    n->len=(size_t)k; return 0;
}
static int dirs(int stagefd,int manifestfd,struct stat *stage,struct stat *manifest) {
    if (fstat(stagefd,stage) || fstat(manifestfd,manifest) || !S_ISDIR(stage->st_mode) || !S_ISDIR(manifest->st_mode) ||
        stage->st_dev!=manifest->st_dev || (stage->st_dev==manifest->st_dev && stage->st_ino==manifest->st_ino) ||
        stage->st_uid!=geteuid() || manifest->st_uid!=geteuid()) { errno=EINVAL; return -1; }
    return 0;
}
static int absent(int fd,const char *name) {
    struct stat st;
    if (!fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW)) { errno=EEXIST; return -1; }
    return errno==ENOENT ? 0 : -1;
}
static int fullwrite(int fd,const char *p,size_t n) {
    while (n) { ssize_t w=write(fd,p,n); if (w<=0) return -1; p+=w; n-=(size_t)w; }
    return 0;
}
static int record(char *out,size_t cap,const char *token,const struct stat *file,const struct stat *stage,const struct stat *manifest) {
    int n=snprintf(out,cap,"RME-CANARY-1 %s %" PRIuMAX " %" PRIuMAX " %" PRIuMAX " %" PRIuMAX " %" PRIuMAX " %" PRIuMAX "\n",
        token,(uintmax_t)file->st_dev,(uintmax_t)file->st_ino,(uintmax_t)stage->st_dev,(uintmax_t)stage->st_ino,
        (uintmax_t)manifest->st_dev,(uintmax_t)manifest->st_ino);
    if (n<0 || (size_t)n>=cap) { errno=EOVERFLOW; return -1; } return n;
}
static int exact(int fd,const char *expected,size_t length,struct stat *st) {
    char buf[1024];
    if (length>sizeof buf || fstat(fd,st) || !S_ISREG(st->st_mode) || st->st_nlink!=1 || st->st_uid!=geteuid() || st->st_size!=(off_t)length) { errno=EINVAL; return -1; }
    size_t at=0; while(at<length) { ssize_t r=read(fd,buf+at,length-at); if(r<=0) { errno=EINVAL; return -1; } at+=(size_t)r; }
    if (memcmp(buf,expected,length)) { errno=EINVAL; return -1; } return 0;
}
static int verify_at(int stagefd,int manifestfd,const char *token,const struct names *n,const char *file) {
    struct stat stage,manifest,st,meta_st; char meta[256]; int fd=-1,mfd=-1,rc=-1;
    if (dirs(stagefd,manifestfd,&stage,&manifest)) return -1;
    fd=openat(manifestfd,file,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if(fd<0) goto out;
    if(exact(fd,n->xml,n->len,&st)) goto out;
    int len=record(meta,sizeof meta,token,&st,&stage,&manifest); if(len<0) goto out;
    mfd=openat(stagefd,n->meta,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if(mfd<0) goto out;
    if(exact(mfd,meta,(size_t)len,&meta_st)) goto out;
    rc=0;
out: { int e=errno; if(fd>=0)close(fd); if(mfd>=0)close(mfd); errno=e; return rc; }
}
int rme_canary_publish(int stagefd,int manifestfd,const char *token,rme_canary_guard guard,void *context) {
    struct names n; struct stat stage,manifest,file,confirmed,meta_st; char meta[256]; int fd=-1,mfd=-1,checkfd=-1,checkmeta=-1,rc=-1;
    if(names_for(token,&n) || !guard || guard(context)!=1 || dirs(stagefd,manifestfd,&stage,&manifest) ||
        absent(stagefd,n.temp) || absent(stagefd,n.meta) || absent(manifestfd,n.target)) return -1;
    fd=openat(stagefd,n.temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600); if(fd<0) goto out;
    if(fullwrite(fd,n.xml,n.len) || fchmod(fd,0644) || fsync(fd) || fstat(fd,&file) ||
        !S_ISREG(file.st_mode) || file.st_nlink!=1 || file.st_uid!=geteuid() || file.st_size!=(off_t)n.len) goto out;
    int len=record(meta,sizeof meta,token,&file,&stage,&manifest); if(len<0) goto out;
    mfd=openat(stagefd,n.meta,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600); if(mfd<0) goto out;
    if(fullwrite(mfd,meta,(size_t)len) || fsync(mfd) || fsync(stagefd) || guard(context)!=1) goto out;
    checkfd=openat(stagefd,n.temp,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if(checkfd<0) goto out;
    if(exact(checkfd,n.xml,n.len,&confirmed) || confirmed.st_dev!=file.st_dev || confirmed.st_ino!=file.st_ino) goto out;
    checkmeta=openat(stagefd,n.meta,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if(checkmeta<0) goto out;
    if(exact(checkmeta,meta,(size_t)len,&meta_st) || guard(context)!=1) goto out;
    if(syscall(__NR_renameat2,stagefd,n.temp,manifestfd,n.target,RENAME_NOREPLACE)) goto out;
    if(fsync(stagefd) || fsync(manifestfd)) goto out;
    rc=0;
out: { int e=errno; if(fd>=0)close(fd); if(mfd>=0)close(mfd); if(checkfd>=0)close(checkfd); if(checkmeta>=0)close(checkmeta); errno=e; return rc; }
}
int rme_canary_verify(int stagefd,int manifestfd,const char *token) {
    struct names n; if(names_for(token,&n)) return -1;
    return verify_at(stagefd,manifestfd,token,&n,n.target);
}
int rme_canary_cleanup(int stagefd,int manifestfd,const char *token,rme_canary_guard guard,void *context) {
    struct names n; if(names_for(token,&n) || !guard || guard(context)!=1 || verify_at(stagefd,manifestfd,token,&n,n.target)) return -1;
    if(guard(context)!=1 || unlinkat(manifestfd,n.target,0) || fsync(manifestfd)) return -1;
    if(unlinkat(stagefd,n.meta,0) || fsync(stagefd)) return -1;
    return 0;
}
