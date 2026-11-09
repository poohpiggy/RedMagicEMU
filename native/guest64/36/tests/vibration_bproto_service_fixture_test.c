#define _GNU_SOURCE
#include "vibration_bproto_service_fixture.h"
#include "../bproto.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

enum { BR_TRANSACTION = 0x80407202u, BR_REPLY = 0x80407203u };
struct fixture { struct rme_bproto_client *client; int stop_fd; int result; int on_count, off_count, mode; };
static int fake_on(void *v, int32_t duration) { struct fixture *f=v; assert(duration==500); ++f->on_count; return f->mode==6 ? -1 : 0; }
static int fake_off(void *v) { struct fixture *f=v; ++f->off_count; return f->mode==8 && f->off_count==2 ? -1 : 0; }
static void *service(void *v) {
    struct fixture *f=v;
    struct rme_vib_add_service_input registration = {
        .interface_header = {0,0,0,0,0,0,0,0,'T','S','Y','S'},
        .binder_flags=0x13, .binder_ptr=0x1000, .binder_cookie=0x2000, .stability=0x11223344,
    };
    struct rme_vib_ops ops = { .ctx=f, .on=fake_on, .off=fake_off };
    f->result = rme_test_vib_service(f->client, &registration, &ops, f->stop_fd);
    return NULL;
}
static ssize_t packet(int fd, unsigned char *buf, size_t cap) {
    ssize_t n=recv(fd,buf,cap,0); assert(n>0); return n;
}
static void send_transaction(int fd, uint32_t command, uint32_t code, uint64_t id,
                             const void *data, size_t size, uint64_t ptr, uint32_t flags) {
    struct d_hdr h={ .flags=F_TXN, .cmd=command, .code=code, .ptr=ptr,
                     .cookie=ptr ? 0x2000 : 0, .data_size=size, .id=id, .tflags=flags };
    unsigned char padded[128]={0}; assert(size<=sizeof padded);
    memcpy(padded,data,size);
    struct iovec v[]={{&h,sizeof h},{padded,(size+7)&~(size_t)7}};
    struct msghdr m={.msg_iov=v,.msg_iovlen=2};
    assert(sendmsg(fd,&m,MSG_NOSIGNAL)==(ssize_t)(sizeof h+((size+7)&~(size_t)7)));
}
static size_t make_request(unsigned char *p, int code) {
    static const char token[]="android.hardware.vibrator.IVibrator";
    memset(p,0,120); p[8]='T';p[9]='S';p[10]='Y';p[11]='S';p[12]=35;
    for (size_t i=0;i<sizeof token-1;++i) p[16+2*i]=(unsigned char)token[i];
    if (code!=3) return 88;
    p[88]=0xf4; p[89]=1; p[92]=0x85;p[93]=0x2a;p[94]=0x62;p[95]=0x73;p[96]=0x13;
    return 120;
}
static void run_case(int mode) {
    int s[2], stop[2]; assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,s)==0);
    assert(pipe(stop)==0);
    struct timeval timeout={.tv_sec=3}; assert(setsockopt(s[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)==0);
    struct rme_bproto_client c; assert(rme_bproto_open(&c,s[0],getpid(),getuid(),1000,91)==0);
    unsigned char wire[4096]; assert(packet(s[1],wire,sizeof wire)==sizeof(struct c_hello));
    struct fixture f={.client=&c,.stop_fd=stop[0],.mode=mode}; pthread_t thread;
    assert(pthread_create(&thread,NULL,service,&f)==0);
    ssize_t n=packet(s[1],wire,sizeof wire); assert(n==sizeof(struct c_hdr)+4);
    struct c_hdr *h=(void *)wire; assert(h->op==OP_BCS);
    n=packet(s[1],wire,sizeof wire); assert(n==sizeof(struct c_txn)+208);
    struct c_txn *txn=(void *)wire; assert(txn->h.op==OP_TXN && txn->code==5 && txn->data_size==200 && txn->offsets_size==8);
    n=packet(s[1],wire,sizeof wire); h=(void *)wire; assert(n==sizeof *h && h->op==OP_READ);
    uint32_t success=0;
    if (mode==9) {
        struct d_hdr wake={.flags=F_WAKE};
        struct timespec delay={.tv_sec=0,.tv_nsec=400000000};
        for (int i=0;i<12;++i) {
            if (send(s[1],&wake,sizeof wake,MSG_NOSIGNAL)<0) break;
            nanosleep(&delay,NULL);
        }
        assert(pthread_join(thread,NULL)==0 && f.result<0 && f.on_count==0 && f.off_count==1 && c.fd==-1);
        close(s[1]); close(stop[0]); close(stop[1]); return;
    }
    if (mode==5) {
        char byte=1; assert(write(stop[1],&byte,1)==1);
        assert(pthread_join(thread,NULL)==0 && f.result<0 && f.on_count==0 && f.off_count==1 && c.fd==-1);
        close(s[1]); close(stop[0]); close(stop[1]); return;
    }
    if (mode==4) success=1;
    send_transaction(s[1],BR_REPLY,0,1,&success,4,0,0);
    if (mode==4) {
        assert(pthread_join(thread,NULL)==0 && f.result<0 && f.on_count==0 && f.off_count==1 && c.fd==-1);
        close(s[1]); close(stop[0]); close(stop[1]); return;
    }
    n=packet(s[1],wire,sizeof wire); h=(void *)wire; assert(n==sizeof *h+12 && h->op==OP_BCS);
    n=packet(s[1],wire,sizeof wire); h=(void *)wire; assert(n==sizeof *h && h->op==OP_READ);
    for (int code=1;code<=3;++code) {
        unsigned char data[120]; size_t len=make_request(data,code);
        send_transaction(s[1],BR_TRANSACTION,(uint32_t)code,(uint64_t)(code+1),data,len,0x1000,0);
        if (mode==6 && code==3) {
            assert(pthread_join(thread,NULL)==0 && f.result<0 && f.on_count==1 && f.off_count==2 && c.fd==-1);
            close(s[1]); close(stop[0]); close(stop[1]); return;
        }
        n=packet(s[1],wire,sizeof wire); txn=(void *)wire;
        assert(txn->h.op==OP_TXN && txn->cmd==0x40406301u && txn->data_size==(code==1?8:4));
        for(size_t i=sizeof *txn;i<(size_t)n;++i) assert(wire[i]==0);
        n=packet(s[1],wire,sizeof wire); h=(void *)wire; assert(n==sizeof *h+12 && h->op==OP_BCS);
        n=packet(s[1],wire,sizeof wire); h=(void *)wire; assert(n==sizeof *h && h->op==OP_READ);
    }
    if (mode==1 || mode==2 || mode==3) {
        unsigned char data[120]; size_t len=make_request(data,1);
        send_transaction(s[1],BR_TRANSACTION,mode==3?999:1,99,data,len,mode==1?0x9999:0x1000,mode==2?1:0);
    } else if (mode==7) {
        struct d_hdr event={.br_size=4}; uint32_t unsupported_br=0x0000720du;
        struct iovec v[]={{&event,sizeof event},{&unsupported_br,4}};
        struct msghdr msg={.msg_iov=v,.msg_iovlen=2};
        assert(sendmsg(s[1],&msg,MSG_NOSIGNAL)==(ssize_t)(sizeof event+4));
    } else { char byte=1; assert(write(stop[1],&byte,1)==1); }
    assert(pthread_join(thread,NULL)==0);
    assert((f.result==0)==(mode==0) && f.on_count==1 && f.off_count==2 && c.fd==-1);
    close(s[1]); close(stop[0]); close(stop[1]);
}
int main(void) { for(int mode=0;mode<=9;++mode) run_case(mode); puts("VIBRATION_BPROTO_SERVICE_FIXTURE_OK"); }
