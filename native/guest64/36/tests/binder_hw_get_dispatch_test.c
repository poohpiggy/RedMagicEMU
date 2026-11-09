/* Run as libaemu64_36.so /system/bin/mediaserver /system/bin/mediaserver MODE (0..9). */
#define main binderd_unused_main
#include "../binderd.c"
#undef main
#include <assert.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define DS 208u
#define OS 32u
struct test_obj { u32 type, flags; u64 buffer, length, parent, parent_offset; };
static void put32(unsigned char *p, size_t n, u32 x) { memcpy(p+n,&x,4); }
static void put64(unsigned char *p, size_t n, u64 x) { memcpy(p+n,&x,8); }
static size_t a8(size_t n) { return (n+7)&~(size_t)7; }
static size_t payload_init(unsigned char *p) {
    const char token[]="android.hidl.manager@1.0::IServiceManager";
    const char svc[]="android.hardware.media.c2@1.0::IComponentStore";
    const char instance[]="default";
    size_t sl=sizeof svc, il=sizeof instance, at=DS+OS;
    memset(p,0,512); memcpy(p,token,sizeof token);
    struct test_obj objs[4]={{T_PTR,0,0,16,0,0},{T_PTR,1,0,sl,0,0},
                             {T_PTR,0,0,16,0,0},{T_PTR,1,0,il,2,0}};
    for(size_t i=0;i<4;i++){memcpy(p+48+i*40,&objs[i],40);put64(p,DS+i*8,48+i*40);}
    put32(p,at+8,(u32)(sl-1));memcpy(p+at+16,svc,sl);at+=16+a8(sl);
    put32(p,at+8,(u32)(il-1));memcpy(p+at+16,instance,il);at+=16+a8(il);
    return at-(DS+OS);
}
int main(int argc,char **argv){
    if(getenv("RME_HW_CHILD")){for(;;)pause();}
    assert(argc==4 && !strcmp(argv[1],"/system/bin/mediaserver") && !strcmp(argv[2],"/system/bin/mediaserver"));
    int mode=atoi(argv[3]);assert(mode>=0 && mode<=9);
    char exe[1024];ssize_t len=readlink("/proc/self/exe",exe,sizeof(exe)-1);assert(len>0 && len<(ssize_t)sizeof(exe)-1);exe[len]=0;
    pid_t child=fork();assert(child>=0);
    if(!child){assert(prctl(PR_SET_PDEATHSIG,SIGKILL)==0);setenv("RME_HW_CHILD","1",1);const char *g=mode==3?"/system/bin/other":"/system/bin/hwservicemanager";execl(exe,exe,g,g,(char *)NULL);_exit(127);}
    /* Wait for exec's complete cmdline; no hard-coded timing or unbounded wait. */
    int ready=0;
    for(int i=0;i<300;i++){char path[64],buf[1024];snprintf(path,sizeof path,"/proc/%d/cmdline",child);int f=open(path,O_RDONLY|O_CLOEXEC);if(f>=0){ssize_t n=read(f,buf,sizeof buf);close(f);const char *target=mode==3?"/system/bin/other":"/system/bin/hwservicemanager";if(n>0&&memmem(buf,(size_t)n,target,strlen(target))){ready=1;break;}}usleep(10000);}
    assert(ready);
    media_txn_trace_enabled=mode!=0;media_txn_trace_driver=mode==1?1:2;
    int saved=dup(STDERR_FILENO);assert(saved>=0);int logs[2];assert(pipe(logs)==0);assert(dup2(logs[1],STDERR_FILENO)==STDERR_FILENO);close(logs[1]);setvbuf(stderr,NULL,_IONBF,0);
    int cs[2],ss[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,cs)==0);assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,ss)==0);
    struct proc client={.pid=getpid()},server={.pid=child};
    struct thread caller={.fd=cs[0],.tid=(int)syscall(SYS_gettid),.proc=&client,.read_size=4096};
    struct thread callee={.fd=ss[0],.tid=child,.proc=&server,.read_size=4096};
    struct node manager={.owner=&server};ctx_mgr=&manager;
    unsigned char payload[512];size_t extra=payload_init(payload),all=DS+OS+extra;
    if(mode==8) media_txn_trace_state.events=BMT_LIMIT-2; if(mode==9) media_txn_trace_state.events=BMT_LIMIT-1;
    struct c_txn m={0};m.cmd=BC_TRANSACTION;m.code=mode==4?2:1;m.data_size=DS;m.offsets_size=OS;m.extra_size=extra;
    if(mode==5){int fd=(int)syscall(SYS_memfd_create,"synthetic",0);assert(fd>=0&&write(fd,payload,all)==(ssize_t)all);m.h.flags=F_BIG;do_txn(&caller,&m,NULL,0,&fd,1);}
    else do_txn(&caller,&m,(char *)payload,mode==6?all-1:all,NULL,0);
    assert(caller.stack);callee.waiting=1;deliver(&callee);
    unsigned char received[2048],control[CMSG_SPACE(sizeof(int))];struct iovec iov={received,sizeof received};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
    ssize_t n=recvmsg(ss[1],&msg,0);assert(n>=(ssize_t)sizeof(struct d_hdr));struct d_hdr *h=(struct d_hdr *)received;assert(h->cmd==BR_TRANSACTION && h->code==m.code);
    if(mode==5){struct cmsghdr*c=CMSG_FIRSTHDR(&msg);assert(c&&c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS);int fd;memcpy(&fd,CMSG_DATA(c),sizeof fd);unsigned char copy[512];assert(pread(fd,copy,all,0)==(ssize_t)all);assert(!memcmp(copy,payload,all));close(fd);}
    else {assert(n>=(ssize_t)(sizeof(*h)+all));assert(!memcmp(received+sizeof(*h)+h->br_size,payload,all));}
    if(mode==7){
        struct thread more[2]={{.fd=cs[0],.tid=(int)syscall(SYS_gettid),.proc=&client,.read_size=4096},
                               {.fd=cs[0],.tid=(int)syscall(SYS_gettid),.proc=&client,.read_size=4096}};
        do_txn(&more[0],&m,(char *)payload,all,NULL,0);assert(more[0].stack);
        payload[all-8]='X';
        do_txn(&more[1],&m,(char *)payload,all,NULL,0);assert(more[1].stack);
    }
    close(STDERR_FILENO);char out[4096];n=read(logs[0],out,sizeof(out)-1);assert(n>=0);out[n]=0;assert(dup2(saved,STDERR_FILENO)==STDERR_FILENO);close(saved);
    assert(!strstr(out,"android.hardware")&&!strstr(out,"IServiceManager")&&!strstr(out,"default"));
    char *classification=strstr(out,"phase=hw-get-class");
    if(mode==2||mode==8){assert(classification&&strstr(classification,"detail=514"));}
    else if(mode==5||mode==6){assert(classification&&strstr(classification,"detail=0"));}
    else if(mode==7){assert(classification&&strstr(classification,"detail=514"));char *second=strstr(classification+1,"phase=hw-get-class");assert(second&&strstr(second,"detail=514"));char *third=strstr(second+1,"phase=hw-get-class");assert(third&&strstr(third,"detail=513")&&!strstr(third+1,"phase=hw-get-class"));}
    else assert(!classification);
    if(mode==8||mode==9)assert(media_txn_trace_state.events==BMT_LIMIT && strstr(out,"event limit reached"));
    kill(child,SIGKILL);assert(waitpid(child,NULL,0)==child);
    return 0;
}
