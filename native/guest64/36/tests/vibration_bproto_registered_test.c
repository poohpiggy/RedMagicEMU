#define _GNU_SOURCE
#include "../vibration_bproto_step.h"
#include "../bproto.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct result { struct rme_vib_step *s; struct rme_vib_step_event event; int rc; };
static int live(void *p) { (void)p; return 1; }
static void *run(void *p) { struct result *r=p; r->rc=rme_vib_step_next(r->s,&r->event); return NULL; }
static void reply(int fd, uint32_t status) {
    struct d_hdr h={.flags=F_TXN,.cmd=0x80407203u,.data_size=4,.id=1};
    uint64_t data=status; struct iovec v[]={{&h,sizeof h},{&data,sizeof data}};
    struct msghdr m={.msg_iov=v,.msg_iovlen=2};
    assert(sendmsg(fd,&m,MSG_NOSIGNAL)==(ssize_t)(sizeof h+sizeof data));
}
int main(void) {
    int s[2]; assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,s));
    struct rme_bproto_client c; assert(!rme_bproto_open(&c,s[0],getpid(),getuid(),1000,77));
    unsigned char packet[4096];assert(recv(s[1],packet,sizeof packet,0)>0);
    struct rme_vib_add_service_input in={.binder_ptr=1,.binder_cookie=2};
    struct rme_vib_step step;assert(!rme_vib_step_init(&step,&c,&in,live,NULL));
    assert(recv(s[1],packet,sizeof packet,0)>0);assert(recv(s[1],packet,sizeof packet,0)>0);
    struct result r={.s=&step};pthread_t t;assert(!pthread_create(&t,NULL,run,&r));
    assert(recv(s[1],packet,sizeof packet,0)>0);reply(s[1],0);
    struct timespec until;assert(!clock_gettime(CLOCK_REALTIME,&until));until.tv_sec+=2;
    assert(!pthread_timedjoin_np(t,NULL,&until));
    assert(r.rc==3 && !r.event.token && !r.event.code && !r.event.duration_ms);
    assert(recv(s[1],packet,sizeof packet,0)>0); /* registration buffer released */
    assert(!pthread_create(&t,NULL,run,&r));
    assert(recv(s[1],packet,sizeof packet,0)>0); /* second call waits rather than reporting again */
    assert(!rme_vib_step_stop(&step));assert(!pthread_join(t,NULL));
    assert(r.rc==RME_VIB_STEP_STOPPED);
    rme_vib_step_destroy(&step);close(s[1]);

    assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,s));
    assert(!rme_bproto_open(&c,s[0],getpid(),getuid(),1000,78));
    assert(recv(s[1],packet,sizeof packet,0)>0);
    assert(!rme_vib_step_init(&step,&c,&in,live,NULL));
    assert(recv(s[1],packet,sizeof packet,0)>0);assert(recv(s[1],packet,sizeof packet,0)>0);
    r=(struct result){.s=&step};assert(!pthread_create(&t,NULL,run,&r));
    assert(recv(s[1],packet,sizeof packet,0)>0);reply(s[1],1);
    assert(!pthread_join(t,NULL));assert(r.rc<0 && c.fd==-1);
    rme_vib_step_destroy(&step);close(s[1]);
    puts("VIBRATION_BPROTO_REGISTERED_OK");
}
