#define _GNU_SOURCE
#include "../canary_fragment.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int stopped(void *p) { return *(int *)p; }
static int stops_between(void *p) { return ++*(int *)p == 1; }
int main(int argc, char **argv) {
    assert(argc == 2); assert(!mkdir(argv[1], 0700));
    int base=open(argv[1],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); assert(base>=0);
    assert(!mkdirat(base,"stage",0700) && !mkdirat(base,"manifest",0700));
    int stage=openat(base,"stage",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int manifest=openat(base,"manifest",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    assert(stage>=0 && manifest>=0);
    const char *token="0123456789abcdef0123456789abcdef";
    const char *name="aemu-canary-0123456789abcdef0123456789abcdef.xml";
    int ok=0;
    assert(rme_canary_publish(stage,manifest,token,stopped,&ok)<0);
    struct stat st; errno=0; assert(fstatat(manifest,name,&st,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
    ok=1; assert(rme_canary_publish(stage,manifest,"slot1",stopped,&ok)<0);
    int occupied=openat(manifest,name,O_CREAT|O_EXCL|O_WRONLY,0600); assert(occupied>=0);
    assert(write(occupied,"existing",8)==8 && !close(occupied));
    assert(rme_canary_publish(stage,manifest,token,stopped,&ok)<0);
    occupied=openat(manifest,name,O_RDONLY|O_NOFOLLOW); char b[16]={0};
    assert(occupied>=0 && read(occupied,b,sizeof b)==8 && !memcmp(b,"existing",8) && !close(occupied));
    assert(!unlinkat(manifest,name,0));
    assert(!rme_canary_publish(stage,manifest,token,stopped,&ok));
    assert(!rme_canary_verify(stage,manifest,token));
    ok=0; assert(rme_canary_cleanup(stage,manifest,token,stopped,&ok)<0); ok=1;
    int fd=openat(manifest,name,O_WRONLY|O_APPEND|O_NOFOLLOW); assert(fd>=0 && write(fd,"x",1)==1 && !close(fd));
    assert(rme_canary_verify(stage,manifest,token)<0);
    assert(rme_canary_cleanup(stage,manifest,token,stopped,&ok)<0);
    fd=openat(manifest,name,O_WRONLY|O_NOFOLLOW); assert(fd>=0 && !fstat(fd,&st));
    assert(!ftruncate(fd,st.st_size-1) && !close(fd));
    assert(!rme_canary_verify(stage,manifest,token));
    assert(!renameat(manifest,name,manifest,"original"));
    fd=openat(manifest,name,O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd>=0 && !close(fd));
    assert(rme_canary_cleanup(stage,manifest,token,stopped,&ok)<0);
    assert(!unlinkat(manifest,name,0) && !renameat(manifest,"original",manifest,name));
    assert(!rme_canary_cleanup(stage,manifest,token,stopped,&ok));
    errno=0; assert(fstatat(manifest,name,&st,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
    const char *tmp=".rme-canary-0123456789abcdef0123456789abcdef.stage";
    const char *meta=".rme-canary-0123456789abcdef0123456789abcdef.meta";
    assert(!symlinkat("outside",stage,tmp));
    assert(rme_canary_publish(stage,manifest,token,stopped,&ok)<0);
    assert(!fstatat(stage,tmp,&st,AT_SYMLINK_NOFOLLOW) && S_ISLNK(st.st_mode));
    assert(!unlinkat(stage,tmp,0));
    int calls=0; assert(rme_canary_publish(stage,manifest,token,stops_between,&calls)<0);
    assert(!fstatat(stage,tmp,&st,AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode));
    assert(!fstatat(stage,meta,&st,AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode));
    errno=0; assert(fstatat(manifest,name,&st,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
    /* Test-owned artifacts only; production error handling intentionally preserves them. */
    assert(!unlinkat(stage,tmp,0) && !unlinkat(stage,meta,0));
    assert(!close(stage) && !close(manifest));
    assert(!unlinkat(base,"stage",AT_REMOVEDIR) && !unlinkat(base,"manifest",AT_REMOVEDIR));
    assert(!close(base) && !rmdir(argv[1]));
    puts("CANARY_FRAGMENT_TEST_OK"); return 0;
}
