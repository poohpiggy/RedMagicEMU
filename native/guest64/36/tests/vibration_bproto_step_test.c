#define _GNU_SOURCE
#include "../vibration_bproto_step.h"
#include "../bproto.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

struct context { struct rme_vib_step *step; struct rme_vib_step_event event; int result; };
struct race { struct rme_vib_step *step; uint64_t token; _Atomic int start; int commit_result,stop_result; };
static int live(void *ctx) { return atomic_load((_Atomic int *)ctx); }
static void *worker(void *p) { struct context *c=p; c->result=rme_vib_step_next(c->step,&c->event); return NULL; }
static void *commit_worker(void *p) { struct race *r=p; while(!atomic_load(&r->start)) sched_yield();r->commit_result=rme_vib_step_complete(r->step,r->token,1);return NULL; }
static void *stop_worker(void *p) { struct race *r=p; while(!atomic_load(&r->start)) sched_yield();r->stop_result=rme_vib_step_stop(r->step);return NULL; }
static ssize_t recv_packet(int fd,void *p,size_t n) { ssize_t r=recv(fd,p,n,0);assert(r>0);return r; }
static void respond(int fd,uint32_t cmd,uint32_t code,uint64_t id,const void *data,size_t size,uint64_t ptr,uint32_t tflags) {
    struct d_hdr h={.flags=F_TXN,.cmd=cmd,.code=code,.data_size=size,.id=id,.ptr=ptr,.cookie=ptr?0x2000:0,.tflags=tflags};
    unsigned char p[128]={0};assert(size<=sizeof p);memcpy(p,data,size);
    struct iovec v[]={{&h,sizeof h},{p,(size+7)&~(size_t)7}};struct msghdr m={.msg_iov=v,.msg_iovlen=2};
    assert(sendmsg(fd,&m,MSG_NOSIGNAL)==(ssize_t)(sizeof h+((size+7)&~(size_t)7)));
}
static void on_request(unsigned char p[120]) {
    const char name[]="android.hardware.vibrator.IVibrator";
    memset(p,0,120);p[8]='T';p[9]='S';p[10]='Y';p[11]='S';p[12]=35;
    for(size_t i=0;i<sizeof name-1;++i)p[16+2*i]=(unsigned char)name[i];
    p[88]=0xf4;p[89]=1;p[92]=0x85;p[93]=0x2a;p[94]=0x62;p[95]=0x73;p[96]=0x13;
}
static void setup(struct rme_vib_step *step,struct rme_bproto_client *c,int s[2],_Atomic int *peer) {
    assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,s)==0);
    struct timeval tv={.tv_sec=3};assert(setsockopt(s[1],SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv)==0);
    assert(rme_bproto_open(c,s[0],getpid(),getuid(),1000,505)==0);
    unsigned char packet[4096];assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hello));
    struct rme_vib_add_service_input in={.interface_header={0,0,0,0,0,0,0,0,'T','S','Y','S'},
      .binder_flags=0x13,.binder_ptr=0x1000,.binder_cookie=0x2000,.stability=0x11223344};
    assert(rme_vib_step_init(step,c,&in,live,peer)==0);
    ssize_t n=recv_packet(s[1],packet,sizeof packet);assert(n==sizeof(struct c_hdr)+4 && ((struct c_hdr*)packet)->op==OP_BCS);
    n=recv_packet(s[1],packet,sizeof packet);assert(n==sizeof(struct c_txn)+208 && ((struct c_txn*)packet)->code==5 && ((struct c_txn*)packet)->tflags==0x10);
}
static void start_and_register(struct rme_vib_step *step,int peerfd,pthread_t *thread,struct context *ctx) {
    unsigned char packet[4096];ctx->step=step;assert(pthread_create(thread,NULL,worker,ctx)==0);
    assert(recv_packet(peerfd,packet,sizeof packet)==sizeof(struct c_hdr) && ((struct c_hdr*)packet)->op==OP_READ);
    uint32_t ok=0;respond(peerfd,0x80407203u,0,1,&ok,4,0,0);
    assert(recv_packet(peerfd,packet,sizeof packet)==sizeof(struct c_hdr)+12 && ((struct c_hdr*)packet)->op==OP_BCS);
    assert(!pthread_join(*thread,NULL) && ctx->result==RME_VIB_STEP_REGISTERED);
    assert(ctx->event.token==0 && ctx->event.code==0 && ctx->event.duration_ms==0);
    assert(!pthread_create(thread,NULL,worker,ctx));
    assert(recv_packet(peerfd,packet,sizeof packet)==sizeof(struct c_hdr) && ((struct c_hdr*)packet)->op==OP_READ);
}
static void run_case(int mode) {
    int s[2];_Atomic int peer=1;struct rme_vib_step step;struct rme_bproto_client c;
    setup(&step,&c,s,&peer);pthread_t thread;struct context ctx={0};start_and_register(&step,s[1],&thread,&ctx);
    if(mode==4) {
        assert(rme_vib_step_stop(&step)==0 && pthread_join(thread,NULL)==0 && ctx.result==RME_VIB_STEP_STOPPED);
    } else if(mode==11) {
        /* TF_ACCEPT_FDS is a protocol bit, not permission for this bridge to
         * accept a descriptor. Any actual passed FD must close the session. */
        unsigned char data[120];on_request(data);
        struct d_hdr h={.flags=F_TXN,.cmd=0x80407202u,.code=3,.tflags=0x10,.data_size=120,.id=2,.ptr=0x1000,.cookie=0x2000,.nfds=1};
        struct iovec v[]={{&h,sizeof h},{data,sizeof data}};char control[CMSG_SPACE(sizeof(int))]={0};
        struct msghdr m={.msg_iov=v,.msg_iovlen=2,.msg_control=control,.msg_controllen=sizeof control};
        struct cmsghdr *ch=CMSG_FIRSTHDR(&m);ch->cmsg_level=SOL_SOCKET;ch->cmsg_type=SCM_RIGHTS;ch->cmsg_len=CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(ch),&s[1],sizeof(int));assert(sendmsg(s[1],&m,MSG_NOSIGNAL)==(ssize_t)(sizeof h+sizeof data));
        assert(pthread_join(thread,NULL)==0 && ctx.result<0 && c.fd==-1);
    } else {
        if(mode==5) { struct rme_vib_step_event concurrent;errno=0;assert(rme_vib_step_next(&step,&concurrent)<0 && errno==EBUSY); }
        if(mode==7) atomic_store(&peer,0);
        unsigned char data[120];on_request(data);respond(s[1],0x80407202u,3,2,data,120,0x1000,mode==9?0:(mode==10?0x11:0x10));
        if(mode==10) {
            assert(pthread_join(thread,NULL)==0 && ctx.result<0 && c.fd==-1);
            rme_vib_step_destroy(&step);close(s[1]);return;
        }
        if(mode==7) {
            assert(pthread_join(thread,NULL)==0 && ctx.result<0 && c.fd==-1);
            rme_vib_step_destroy(&step);close(s[1]);return;
        }
        assert(pthread_join(thread,NULL)==0 && ctx.result==RME_VIB_STEP_EVENT);
        assert(ctx.event.code==3 && ctx.event.duration_ms==500 && ctx.event.token);
        if(mode==8) {
            struct race race={.step=&step,.token=ctx.event.token};pthread_t commit,stop;
            assert(pthread_create(&commit,NULL,commit_worker,&race)==0 && pthread_create(&stop,NULL,stop_worker,&race)==0);
            atomic_store(&race.start,1);assert(pthread_join(commit,NULL)==0 && pthread_join(stop,NULL)==0 && race.stop_result==0);
            assert(race.commit_result==0 || race.commit_result==RME_VIB_STEP_STOPPED);
            if(race.commit_result==0) {
                unsigned char packet[4096];assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_txn)+4);
                assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
            } else { char byte;assert(recv(s[1],&byte,1,MSG_DONTWAIT)==0); }
            rme_vib_step_destroy(&step);close(s[1]);return;
        }
        if(mode==2) assert(rme_vib_step_stop(&step)==0);
        if(mode==3) atomic_store(&peer,0);
        int rc=rme_vib_step_complete(&step,ctx.event.token,mode!=6);
        if(mode==2) assert(rc==RME_VIB_STEP_STOPPED);
        else if(mode==3 || mode==6) assert(rc<0);
        else {
            assert(rc==0);unsigned char packet[4096];
            ssize_t n=recv_packet(s[1],packet,sizeof packet);struct c_txn *m=(void *)packet;
            assert(n==sizeof *m+4 && m->cmd==0x40406301u && m->data_size==4);
            assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
            if(mode==1) assert(rme_vib_step_complete(&step,ctx.event.token,1)<0);
            else assert(rme_vib_step_stop(&step)==0);
        }
    }
    rme_vib_step_destroy(&step); assert(c.fd==-1); close(s[1]);
}
static void initial_dead_peer(void) {
    int s[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,s)==0);
    struct rme_bproto_client c;assert(rme_bproto_open(&c,s[0],getpid(),getuid(),1000,600)==0);
    struct c_hello h;assert(recv_packet(s[1],&h,sizeof h)==sizeof h);
    struct rme_vib_add_service_input in={.binder_ptr=1,.binder_cookie=2};
    _Atomic int peer=0;struct rme_vib_step step;
    assert(rme_vib_step_init(&step,&c,&in,live,&peer)<0 && c.fd==-1);close(s[1]);
}
static void interface_transaction_then_on(void) {
    int s[2]; _Atomic int peer=1; struct rme_vib_step step; struct rme_bproto_client c;
    setup(&step,&c,s,&peer); pthread_t thread; struct context ctx={0};
    start_and_register(&step,s[1],&thread,&ctx);
    unsigned char empty=0, packet[4096];
    respond(s[1],0x80407202u,0x5f4e5446u,2,&empty,0,0x1000,0x10);
    ssize_t n=recv_packet(s[1],packet,sizeof packet); struct c_txn *reply=(void *)packet;
    assert(n==sizeof *reply+76 && reply->cmd==0x40406301u && reply->data_size==76);
    unsigned char *data=packet+sizeof *reply;
    assert(data[0]==35 && data[1]==0 && data[2]==0 && data[3]==0);
    const char name[]="android.hardware.vibrator.IVibrator";
    for(size_t i=0;i<sizeof name-1;++i) assert(data[4+2*i]==(unsigned char)name[i] && !data[5+2*i]);
    assert(!data[74] && !data[75]);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
    /* Binder extension lookup: null strong Binder, no object offsets. */
    respond(s[1],0x80407202u,0x5f455854u,3,&empty,0,0x1000,0x10);
    n=recv_packet(s[1],packet,sizeof packet); reply=(void *)packet;
    assert(n==sizeof *reply+28 && reply->cmd==0x40406301u && reply->data_size==28 && reply->offsets_size==0);
    data=packet+sizeof *reply;
    assert(data[0]==0x85 && data[1]==0x2a && data[2]==0x62 && data[3]==0x73);
    assert(data[4]==0x13 && !data[5] && !data[6] && !data[7]);
    for(size_t i=8;i<28;++i) assert(!data[i]);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
    unsigned char on[120]; on_request(on);
    /* No predefined effects or composition primitives. */
    const unsigned arrays[]={10,5}; uint64_t id=4;
    for(size_t j=0;j<sizeof arrays/sizeof arrays[0];++j) {
        respond(s[1],0x80407202u,arrays[j],id++,on,88,0x1000,0x10);
        n=recv_packet(s[1],packet,sizeof packet); reply=(void *)packet;
        assert(n==sizeof *reply+8 && reply->cmd==0x40406301u && reply->data_size==8);
        data=packet+sizeof *reply; for(size_t i=0;i<8;++i) assert(!data[i]);
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
    }
    /* Unsupported numeric queries return Android's EX_UNSUPPORTED_OPERATION,
     * empty String16 message and empty remote stack trace header. */
    const unsigned unsupported[]={8,21,9,22,19,16,18,17,20,23,28,29,27,26};
    for(size_t j=0;j<sizeof unsupported/sizeof unsupported[0];++j) {
        respond(s[1],0x80407202u,unsupported[j],id++,on,88,0x1000,0x10);
        n=recv_packet(s[1],packet,sizeof packet); reply=(void *)packet;
        assert(n==sizeof *reply+16 && reply->cmd==0x40406301u && reply->data_size==16);
        data=packet+sizeof *reply;
        assert(data[0]==0xf9 && data[1]==0xff && data[2]==0xff && data[3]==0xff);
        for(size_t i=4;i<16;++i) assert(!data[i]);
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
    }
    /* Captured native on(2000, null) uses null Binder flags 0. */
    on[88]=0xd0; on[89]=0x07; on[96]=0;
    respond(s[1],0x80407202u,3,id,on,sizeof on,0x1000,0x10);
    assert(!pthread_join(thread,NULL) && ctx.result==RME_VIB_STEP_EVENT);
    assert(ctx.event.code==3 && ctx.event.duration_ms==2000);
    assert(!rme_vib_step_complete(&step,ctx.event.token,1));
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_txn)+4);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    assert(!rme_vib_step_stop(&step)); rme_vib_step_destroy(&step); close(s[1]);
}
static void reject_malformed_interface_transaction(uint32_t code, int mode) {
    int s[2]; _Atomic int peer=1; struct rme_vib_step step; struct rme_bproto_client c;
    setup(&step,&c,s,&peer); pthread_t thread; struct context ctx={0};
    start_and_register(&step,s[1],&thread,&ctx);
    unsigned char byte=0;
    respond(s[1],0x80407202u,code,2,&byte,mode==0?1:0,0x1000,mode==1?0x11:0x10);
    assert(!pthread_join(thread,NULL) && ctx.result<0 && c.fd==-1);
    rme_vib_step_destroy(&step); close(s[1]);
}
static void repeated_on_off_on(void) {
    int s[2]; _Atomic int peer=1; struct rme_vib_step step; struct rme_bproto_client c;
    setup(&step,&c,s,&peer); pthread_t thread; struct context ctx={0};
    start_and_register(&step,s[1],&thread,&ctx);
    unsigned char on[120], packet[4096]; on_request(on);
    const unsigned codes[]={3,2,3};
    for(size_t i=0;i<3;++i) {
        if(i) {
            assert(!pthread_create(&thread,NULL,worker,&ctx));
            assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
        }
        respond(s[1],0x80407202u,codes[i],i+2,on,codes[i]==3?120:88,0x1000,0x10);
        assert(!pthread_join(thread,NULL) && ctx.result==RME_VIB_STEP_EVENT);
        assert(ctx.event.code==codes[i] && ctx.event.duration_ms==(codes[i]==3?500:0));
        assert(!rme_vib_step_complete(&step,ctx.event.token,1));
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_txn)+4);
        assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    }
    assert(!rme_vib_step_stop(&step)); rme_vib_step_destroy(&step); close(s[1]);
}
static void unsupported_perform_then_on(void) {
    int s[2]; _Atomic int peer=1; struct rme_vib_step step; struct rme_bproto_client c;
    setup(&step,&c,s,&peer); pthread_t thread; struct context ctx={0};
    start_and_register(&step,s[1],&thread,&ctx);
    unsigned char data[124]={0}, packet[4096], on[120]; on_request(on);
    memcpy(data,on,88); data[88]=1; data[92]=2; /* effect, strength */
    memcpy(data+96,on+92,28); /* null callback */
    respond(s[1],0x80407202u,4,2,data,sizeof data,0x1000,0x10);
    ssize_t n=recv_packet(s[1],packet,sizeof packet); struct c_txn *reply=(void *)packet;
    assert(n==sizeof *reply+16 && reply->cmd==0x40406301u && reply->data_size==16);
    const unsigned char unsupported[16]={0xf9,0xff,0xff,0xff};
    assert(!memcmp(packet+sizeof *reply,unsupported,sizeof unsupported));
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr));
    respond(s[1],0x80407202u,3,3,on,sizeof on,0x1000,0x10);
    assert(!pthread_join(thread,NULL) && ctx.result==RME_VIB_STEP_EVENT && ctx.event.code==3);
    assert(!rme_vib_step_complete(&step,ctx.event.token,1));
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_txn)+4);
    assert(recv_packet(s[1],packet,sizeof packet)==sizeof(struct c_hdr)+12);
    assert(!rme_vib_step_stop(&step)); rme_vib_step_destroy(&step); close(s[1]);
}
int main(void) { for(int mode=0;mode<=11;++mode) run_case(mode);initial_dead_peer();interface_transaction_then_on();repeated_on_off_on();unsupported_perform_then_on();reject_malformed_interface_transaction(0x5f4e5446u,0);reject_malformed_interface_transaction(0x5f4e5446u,1);reject_malformed_interface_transaction(0x5f455854u,0);reject_malformed_interface_transaction(0x5f455854u,1);puts("VIBRATION_BPROTO_STEP_OK"); }
