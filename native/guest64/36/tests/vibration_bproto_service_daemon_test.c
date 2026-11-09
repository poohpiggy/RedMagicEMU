#define _GNU_SOURCE
#include "vibration_bproto_service_fixture.h"
#include "../vibration_bproto_lifecycle.h"
#include "../bproto.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

struct service { struct rme_bproto_client *client; int stop, result, on_count, off_count; };
static int on(void *ctx,int32_t ms) { struct service *s=ctx; assert(ms==500); ++s->on_count; return 0; }
static int off(void *ctx) { struct service *s=ctx; ++s->off_count; return 0; }
static void *run_service(void *ctx) {
    struct service *s=ctx;
    struct rme_vib_add_service_input in={.interface_header={0,0,0,0,0,0,0,0,'T','S','Y','S'},
        .binder_flags=0x13,.binder_ptr=0x1000,.binder_cookie=0x2000,.stability=0x11223344};
    struct rme_vib_ops ops={.ctx=s,.on=on,.off=off};
    s->result=rme_test_vib_service(s->client,&in,&ops,s->stop);
    return NULL;
}
static int connect_daemon(const char *path) {
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0); assert(fd>=0);
    struct sockaddr_un sa={.sun_family=AF_UNIX}; assert(strlen(path)<sizeof sa.sun_path); strcpy(sa.sun_path,path);
    assert(connect(fd,(struct sockaddr *)&sa,sizeof sa)==0);
    struct timeval timeout={.tv_sec=5}; assert(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)==0);
    return fd;
}
static void command(struct rme_bproto_client *c,uint32_t code,uint32_t arg,int with_arg) {
    struct c_hdr h={.op=OP_BCS,.euid=c->guest_uid};
    struct iovec v[]={{&h,sizeof h},{&code,4},{&arg,with_arg?4:0}};
    struct msghdr m={.msg_iov=v,.msg_iovlen=3};
    assert(sendmsg(c->fd,&m,MSG_NOSIGNAL)==(ssize_t)(sizeof h+4+(with_arg?4:0)));
}
static void reply_handle(struct rme_bproto_client *c,uint32_t handle) {
    struct c_txn m={.h={.op=OP_TXN,.euid=c->guest_uid},.cmd=0x40406301u,.data_size=24,.offsets_size=8};
    struct {uint32_t type,flags; uint64_t binder,cookie;} obj={0x73682a85u,0x13,handle,0};
    uint64_t offset=0; struct iovec v[]={{&m,sizeof m},{&obj,sizeof obj},{&offset,sizeof offset}};
    struct msghdr msg={.msg_iov=v,.msg_iovlen=3};
    assert(sendmsg(c->fd,&msg,MSG_NOSIGNAL)==(ssize_t)(sizeof m+sizeof obj+sizeof offset));
}
static void read_transaction(struct rme_bproto_client *c,struct rme_vib_lifecycle *l,
                             struct d_hdr *h,unsigned char *buf,size_t cap,size_t *length) {
    for(int i=0;i<12;++i) {
        assert(rme_bproto_read_request(c,4096)==0);
        assert(rme_bproto_receive(c,h,buf,cap,length)==0);
        assert(rme_vib_lifecycle_observe(l,h,buf,*length)==0);
        if(h->flags&F_TXN) return;
    }
    assert(!"transaction did not arrive");
}
static size_t make_request(unsigned char *p,int code) {
    static const char name[]="android.hardware.vibrator.IVibrator";
    memset(p,0,120); p[8]='T';p[9]='S';p[10]='Y';p[11]='S';p[12]=35;
    for(size_t i=0;i<sizeof name-1;++i)p[16+2*i]=(unsigned char)name[i];
    if(code!=3)return 88;
    p[88]=0xf4;p[89]=1;p[92]=0x85;p[93]=0x2a;p[94]=0x62;p[95]=0x73;p[96]=0x13;return 120;
}
int main(int argc,char **argv) {
    assert(argc==3); pid_t pid=(pid_t)strtol(argv[2],NULL,10);
    struct rme_bproto_client manager,owner,caller;
    assert(rme_bproto_open(&manager,connect_daemon(argv[1]),pid,getuid(),1000,301)==0);
    assert(rme_bproto_open(&owner,connect_daemon(argv[1]),pid,getuid(),1000,302)==0);
    assert(rme_bproto_open(&caller,connect_daemon(argv[1]),pid,getuid(),1000,303)==0);
    command(&manager,0x0000633eu,0,0); /* only fake manager acts as context manager */
    struct c_hdr poll={.op=OP_READ,.flags=F_READ|F_NONBLOCK,.read_size=4096,.euid=1000};
    assert(send(manager.fd,&poll,sizeof poll,MSG_NOSIGNAL)==sizeof poll);
    struct d_hdr h; unsigned char buf[4096]; size_t length;
    assert(rme_bproto_receive(&manager,&h,buf,sizeof buf,&length)==0 && (h.flags&F_AGAIN));
    struct rme_vib_lifecycle mgr,cli; rme_vib_lifecycle_init(&mgr,&manager,0,0); rme_vib_lifecycle_init(&cli,&caller,0,0);
    int stop[2]; assert(pipe(stop)==0); struct service service={.client=&owner,.stop=stop[0]};
    pthread_t thread; assert(pthread_create(&thread,NULL,run_service,&service)==0);
    read_transaction(&manager,&mgr,&h,buf,sizeof buf,&length);
    assert(h.code==5 && h.data_size==200 && h.offsets_size==8);
    uint32_t type,manager_handle; memcpy(&type,buf+h.br_size+164,4); memcpy(&manager_handle,buf+h.br_size+172,4);
    assert(type==0x73682a85u && manager_handle);
    command(&manager,0x40046305u,manager_handle,1); /* BC_ACQUIRE: retain beyond received buffer */
    const uint32_t ok=0; assert(rme_bproto_reply(&manager,&ok,4)==0 && rme_vib_lifecycle_free(&mgr)==0);

    /* A fake getService transaction returns the registered object to the caller. */
    const uint64_t fake_get=0; assert(rme_bproto_txn(&caller,0,999,0,&fake_get,8,NULL,0)==0);
    read_transaction(&manager,&mgr,&h,buf,sizeof buf,&length); assert(h.code==999);
    reply_handle(&manager,manager_handle); assert(rme_vib_lifecycle_free(&mgr)==0);
    read_transaction(&caller,&cli,&h,buf,sizeof buf,&length); assert(h.data_size==24 && h.offsets_size==8);
    uint32_t caller_handle; memcpy(&type,buf+h.br_size,4); memcpy(&caller_handle,buf+h.br_size+8,4);
    assert(type==0x73682a85u && caller_handle);
    command(&caller,0x40046305u,caller_handle,1); assert(rme_vib_lifecycle_free(&cli)==0);

    for(int code=1;code<=3;++code) {
        unsigned char request[120]; size_t size=make_request(request,code);
        assert(rme_bproto_txn(&caller,caller_handle,(unsigned)code,0,request,size,NULL,0)==0);
        read_transaction(&caller,&cli,&h,buf,sizeof buf,&length);
        assert(h.data_size==(code==1?8u:4u) && h.offsets_size==0);
        for(size_t i=0;i<h.data_size;++i) assert(buf[h.br_size+i]==0);
        assert(rme_vib_lifecycle_free(&cli)==0);
    }
    char signal=1; assert(write(stop[1],&signal,1)==1);
    assert(pthread_join(thread,NULL)==0 && service.result==0 && service.on_count==1 && service.off_count==2 && owner.fd==-1);
    /* Held references must become dead after the service owner disconnects;
     * a later call must not be reported as a successful transaction. */
    unsigned char after_stop[120]; size_t after_stop_size=make_request(after_stop,1);
    assert(rme_bproto_txn(&caller,caller_handle,1,0,after_stop,after_stop_size,NULL,0)==0);
    int dead=0;
    for(int i=0;i<6 && !dead;++i) {
        assert(rme_bproto_read_request(&caller,4096)==0);
        assert(rme_bproto_receive(&caller,&h,buf,sizeof buf,&length)==0);
        assert(!(h.flags&F_TXN) && h.br_size==length && !(h.br_size%4));
        for(size_t j=0;j<h.br_size;j+=4) {
            uint32_t event; memcpy(&event,buf+j,4);
            if(event==0x00007205u || event==0x00007211u) dead=1; /* BR_DEAD_REPLY / BR_FAILED_REPLY */
            else assert(event==0x00007206u); /* BR_TRANSACTION_COMPLETE */
        }
    }
    assert(dead);
    rme_bproto_close(&manager);rme_bproto_close(&caller);close(stop[0]);close(stop[1]);
    puts("VIBRATION_BPROTO_SERVICE_DAEMON_OK");
}
