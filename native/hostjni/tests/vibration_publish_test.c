#define _GNU_SOURCE
#include "../vibration_publish.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(int d, const char *name, const char *value) {
    int fd=openat(d,name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600); assert(fd>=0);
    assert(write(fd,value,strlen(value))==(ssize_t)strlen(value));close(fd);
}
static void same(int d,const char *name,const char *value) {
    char b[64]={0};int fd=openat(d,name,O_RDONLY|O_NOFOLLOW);assert(fd>=0);
    assert(read(fd,b,sizeof b-1)==(ssize_t)strlen(value));close(fd);assert(!strcmp(b,value));
}
int main(void) {
    char path[]="/tmp/rme-vib-publish-XXXXXX";assert(mkdtemp(path));
    int d=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(d>=0);
    put(d,"stage.tmp","first");assert(!rme_vib_publish_noreplace(d,"stage.tmp","target.xml"));
    same(d,"target.xml","first");
    put(d,"stage.tmp","second");assert(rme_vib_publish_noreplace(d,"stage.tmp","target.xml")==-EEXIST);
    same(d,"target.xml","first");same(d,"stage.tmp","second");
    assert(!symlinkat("target.xml",d,"link.tmp"));
    assert(rme_vib_publish_noreplace(d,"link.tmp","other.xml")<0);
    struct stat st;assert(fstatat(d,"link.tmp",&st,AT_SYMLINK_NOFOLLOW)==0 && S_ISLNK(st.st_mode));
    assert(!symlinkat("target.xml",d,"existing-link.xml"));
    assert(rme_vib_publish_noreplace(d,"stage.tmp","existing-link.xml")==-EEXIST);
    assert(fstatat(d,"existing-link.xml",&st,AT_SYMLINK_NOFOLLOW)==0 && S_ISLNK(st.st_mode));
    same(d,"target.xml","first");same(d,"stage.tmp","second");
    assert(!mkdirat(d,"folder",0700));assert(rme_vib_publish_noreplace(d,"folder","other.xml")<0);
    const char *bad[]={"", ".", "..", "../escape", "foo/bar", "foo\\bar", "space name"};
    for (size_t i=0;i<sizeof bad/sizeof bad[0];i++) {
        assert(rme_vib_publish_noreplace(d,bad[i],"other.xml")==-EINVAL);
        assert(rme_vib_publish_noreplace(d,"stage.tmp",bad[i])==-EINVAL);
    }
    assert(rme_vib_publish_noreplace(d,"stage.tmp","stage.tmp")==-EINVAL);
    int f=openat(d,"stage.tmp",O_RDONLY);assert(f>=0);
    assert(rme_vib_publish_noreplace(f,"stage.tmp","other.xml")<0);close(f);
    assert(fstatat(d,"other.xml",&st,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
    assert(!unlinkat(d,"target.xml",0));assert(!unlinkat(d,"stage.tmp",0));
    assert(!unlinkat(d,"link.tmp",0));assert(!unlinkat(d,"existing-link.xml",0));
    assert(!unlinkat(d,"folder",AT_REMOVEDIR));
    close(d);assert(!rmdir(path));puts("VIBRATION_PUBLISH_OK");
}
