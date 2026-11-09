#define _GNU_SOURCE
#include "canary_fragment.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Deliberately pinned to this diagnostic instance; never used by app startup. */
static int walk(int base, const char *path, uid_t uid, int owned) {
    char tmp[256]; if (strlen(path)>=sizeof tmp) { errno=EINVAL; return -1; }
    strcpy(tmp,path); int cur=dup(base); if(cur<0)return -1;
    char *save=NULL;
    for(char *part=strtok_r(tmp,"/",&save);part;part=strtok_r(NULL,"/",&save)) {
        if(!strcmp(part,".") || !strcmp(part,"..")) { close(cur); errno=EINVAL; return -1; }
        int next=openat(cur,part,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); close(cur); if(next<0)return -1;
        struct stat st; if(fstat(next,&st) || !S_ISDIR(st.st_mode) || (owned && st.st_uid!=uid)) { close(next); errno=EINVAL; return -1; }
        cur=next;
    }
    int readable=openat(cur,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); close(cur); return readable;
}
/* The operator alone establishes exclusivity; this guard does not inspect /proc. */
static int manual(void *context) { return *(int *)context; }
int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[1],"inspect") && strcmp(argv[1],"publish") && strcmp(argv[1],"verify") && strcmp(argv[1],"cleanup"))) return 2;
    uid_t uid=getuid(); if(uid!=10434 || geteuid()!=uid) { fprintf(stderr,"unexpected UID\n"); return 3; }
    int root=open("/",O_PATH|O_DIRECTORY|O_CLOEXEC); if(root<0) { perror("root"); return 4; }
    int images=walk(root,"data/user/0/app.redmagicemu.clone/files/images",uid,0); close(root);
    if(images<0) { perror("images"); return 4; }
    int instance=walk(images,"yew75e",uid,1); close(images);
    if(instance<0) { perror("instance"); return 4; }
    int stage=walk(instance,"run",uid,1);
    int manifest=walk(instance,"root/vendor/etc/vintf/manifest",uid,1);
    if(stage<0 || manifest<0) { perror("directories"); if(stage>=0)close(stage); if(manifest>=0)close(manifest); close(instance); return 4; }
    struct stat a,b,c;
    if(fstat(instance,&a)||fstat(stage,&b)||fstat(manifest,&c)) { perror("fstat"); return 4; }
    printf("UID=%u instance=%ju:%ju stage=%ju:%ju manifest=%ju:%ju\n",(unsigned)uid,
        (uintmax_t)a.st_dev,(uintmax_t)a.st_ino,(uintmax_t)b.st_dev,(uintmax_t)b.st_ino,(uintmax_t)c.st_dev,(uintmax_t)c.st_ino);
    int allowed=1, rc=0;
    if(!strcmp(argv[1],"publish")) rc=rme_canary_publish(stage,manifest,argv[2],manual,&allowed);
    else if(!strcmp(argv[1],"verify")) rc=rme_canary_verify(stage,manifest,argv[2]);
    else if(!strcmp(argv[1],"cleanup")) rc=rme_canary_cleanup(stage,manifest,argv[2],manual,&allowed);
    if(rc) perror("canary");
    else printf("CANARY_%s_OK\n",argv[1]);
    close(stage); close(manifest); close(instance); return rc ? 1 : 0;
}
