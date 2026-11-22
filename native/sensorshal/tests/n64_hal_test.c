/* Run against the old HAL with -DTEST_BASELINE to reproduce its inactive flush defect.
 * Without that flag, exercise the N64-specific replacement directly against a real FIFO. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef TEST_BASELINE
#define clock_gettime legacy_clock_gettime
#define nanosleep legacy_nanosleep
#include "../sensors_hal.c"
#undef clock_gettime
#undef nanosleep
int __system_property_get(const char *name, char *value) { (void)name; strcpy(value, "36"); return 2; }
int legacy_clock_gettime(int id, struct timespec_ *t) { (void)id; t->sec = 100; t->nsec = 0; return 0; }
int legacy_nanosleep(const struct timespec_ *a, struct timespec_ *b) { (void)a; (void)b; return 0; }
#else
#define AEMU_SENSORS_FIFO_PATH "/tmp/rme_n64_hal_test.fifo"
#define AEMU_SENSORS_STATE_PATH "/tmp/rme_n64_hal_test.state"
#define AEMU_SENSORS_CAPABILITIES_PATH "/tmp/rme_n64_hal_test.capabilities"
static int wake_fd=-1, wake_errno=0, wake_faults=0;
static ssize_t injected_write(int fd, const void *data, size_t size) {
    if (fd==wake_fd && wake_faults>0) { wake_faults--; errno=wake_errno; return -1; }
    return write(fd,data,size);
}
#define write injected_write
#include "../n64_sensors_hal.c"
#undef write
struct wire_packet { uint32_t magic; int32_t type; int64_t timestamp; float x,y,z; int32_t accuracy; };
_Static_assert(sizeof(struct wire_packet) == 32, "wire packet must be exactly 32 bytes");
struct wire_uncalibrated {
    struct wire_packet motion;
    float bias[3];
    uint32_t reserved;
};
_Static_assert(sizeof(struct wire_uncalibrated) == 48, "uncalibrated record must be exactly 48 bytes");
static void state(int enabled, int64_t epoch) { FILE *f=fopen(AEMU_SENSORS_STATE_PATH,"w"); assert(f); assert(fprintf(f,"%d %lld\n",enabled,(long long)epoch)>0); assert(fclose(f)==0); }
static int64_t now(void) { struct timespec t; assert(clock_gettime(CLOCK_BOOTTIME, &t) == 0); return (int64_t)t.tv_sec*1000000000+t.tv_nsec; }
static void put(int fd, struct wire_packet *p) { assert(write(fd,p,sizeof(*p)) == sizeof(*p)); }
struct request { struct poll_device *device; struct sensors_event_t result; int n; };
static void *reader(void *arg) { struct request *r=arg; r->n=dev_poll(r->device,&r->result,1); return NULL; }
static void join(pthread_t t) { struct timespec until; assert(clock_gettime(CLOCK_REALTIME,&until)==0); until.tv_sec+=2; assert(pthread_timedjoin_np(t,NULL,&until)==0); }
static pthread_t start(struct request *r) { pthread_t t; memset(&r->result,0,sizeof(r->result)); assert(!pthread_create(&t,NULL,reader,r)); return t; }
struct stream { int fd; _Atomic int stop; };
static void *produce(void *arg) {
    struct stream *s=arg;
    while (!s->stop) {
        struct wire_packet p={0x324d4541,1,now(),4,5,6,3}; put(s->fd,&p); usleep(20000);
    }
    return NULL;
}
static int is_flush(struct sensors_event_t *e, int handle) { return e->version == 2 && e->type == 0 && ((int32_t*)e->data)[0] == 1 && ((int32_t*)e->data)[1] == handle; }
#endif
int main(void) {
#ifdef TEST_BASELINE
    struct poll_device device = {0};
    int r = dev_flush(&device, HANDLE_ACCEL);
    if (r != -EINVAL) { fprintf(stderr, "inactive flush: expected -EINVAL, got %d\n", r); return 1; }
#else
    const struct sensor_t *list=NULL;
    unlink(AEMU_SENSORS_CAPABILITIES_PATH);
    assert(read_capabilities()==0);
    FILE *capabilities=fopen(AEMU_SENSORS_CAPABILITIES_PATH,"w");
    assert(capabilities && fputs("2\n",capabilities)>=0 && fclose(capabilities)==0);
    assert(read_capabilities()==2);
    capabilities=fopen(AEMU_SENSORS_CAPABILITIES_PATH,"w");
    assert(capabilities && fputs("3\n",capabilities)>=0 && fclose(capabilities)==0);
    assert(get_list(&HMI,&list)==5 && list);
    unlink(AEMU_SENSORS_CAPABILITIES_PATH);
    assert(list[0].name && list[0].name[0]);
    assert(list[0].vendor && list[0].vendor[0]);
    assert(list[0].stringType && list[0].stringType[0]);
    assert(list[0].requiredPermission && list[0].requiredPermission[0]=='\0');
    assert(list[0].handle==HANDLE_ACCEL && list[0].type==1);
    assert(list[1].handle==HANDLE_GYROSCOPE && list[1].type==4);
    assert(list[2].handle==HANDLE_MAGNETIC_FIELD && list[2].type==2);
    assert(list[3].handle==4 && list[3].type==16);
    assert(list[4].handle==5 && list[4].type==14);
    for (int i=0;i<5;i++) {
        assert(list[i].flags==SENSOR_FLAG_CONTINUOUS_MODE);
        assert((list[i].flags&1u)==0);
    }
    unlink(AEMU_SENSORS_FIFO_PATH); assert(mkfifo(AEMU_SENSORS_FIFO_PATH, 0600)==0);
    struct hw_device *base=NULL;
    assert(sensors_open(&HMI.common,"poll",&base)==0);
    struct poll_device *d=(struct poll_device *)base;
    int writer=open(AEMU_SENSORS_FIFO_PATH,O_WRONLY|O_NONBLOCK); assert(writer>=0);
    assert(dev_flush(d,HANDLE_ACCEL)==-EINVAL);
    assert(dev_activate(d,99,1)==-EINVAL);
    assert(dev_batch(d,99,0,20000000,0)==-EINVAL);
    assert(dev_activate(d,HANDLE_GYROSCOPE,1)==0);
    assert(dev_activate(d,HANDLE_MAGNETIC_FIELD,1)==0);
    assert(dev_batch(d,HANDLE_GYROSCOPE,0,20000000,0)==0);
    assert(dev_batch(d,HANDLE_MAGNETIC_FIELD,0,20000000,0)==0);
    /* A broken wake pipe must report failure without falsely activating. */
    wake_fd=d->wake_write; wake_errno=EBADF; wake_faults=1;
    assert(dev_activate(d,HANDLE_ACCEL,1)==-EBADF);
    assert(dev_flush(d,HANDLE_ACCEL)==-EINVAL);
    assert(dev_activate(d,HANDLE_ACCEL,1)==0);
    assert(dev_batch(d,HANDLE_ACCEL,0,20000000,0)==0);
    wake_errno=EBADF; wake_faults=1; assert(dev_flush(d,1)==-EBADF);
    state(1,now());
    /* A 19.9ms source should not be cut in half by a 20ms requested rate. */
    struct packet rate_sample={0x324d4541,1,now(),1,2,3,3};
    int64_t rate_start=rate_sample.timestamp;
    int rate_delivered=0;
    struct sensors_event_t rate_event;
    for (int i=0;i<6;i++) {
        if (i) usleep(20000);
        rate_sample.timestamp=rate_start+(int64_t)i*19900000LL;
        memcpy(d->bytes,&rate_sample,sizeof(rate_sample));
        rate_delivered+=take_sample(d,&rate_event);
    }
    assert(rate_delivered>=5);
    usleep(25000);
    struct request req={.device=d};
    struct wire_packet p={0x324d4541,1,now(),1.25f,-2.5f,9.75f,2};
    assert(write(writer,&p,7)==7); assert(write(writer,(char*)&p+7,sizeof(p)-7)==sizeof(p)-7);
    pthread_t t=start(&req); join(t);
    assert(req.n==1 && req.result.type==1 && req.result.timestamp==p.timestamp);
    assert(req.result.data[0]==1.25f && req.result.data[1]==-2.5f && req.result.data[2]==9.75f);
    assert(((int8_t*)&req.result.data[3])[0]==2);
    struct wire_packet gyro={0x324d4541,4,now(),0.1f,0.2f,0.3f,3};
    put(writer,&gyro); t=start(&req); join(t);
    assert(req.n==1 && req.result.sensor==HANDLE_GYROSCOPE && req.result.type==4);
    assert(req.result.data[0]==0.1f && req.result.data[1]==0.2f && req.result.data[2]==0.3f);
    struct wire_packet mag={0x324d4541,2,now(),10,20,-30,1};
    put(writer,&mag); t=start(&req); join(t);
    assert(req.n==1 && req.result.sensor==HANDLE_MAGNETIC_FIELD && req.result.type==2);
    assert(req.result.data[0]==10 && req.result.data[1]==20 && req.result.data[2]==-30);
    for (int i=3;i<5;i++) {
        assert(dev_activate(d,list[i].handle,1)==0);
        struct wire_uncalibrated raw={
            {0x334d4541,list[i].type,now(),1.5f,-2.5f,3.5f,0},
            {0.25f,-0.5f,0.75f},0
        };
        struct wire_uncalibrated invalid=raw;
        invalid.bias[1]=NAN;
        assert(write(writer,&invalid,sizeof(invalid))==sizeof(invalid));
        assert(write(writer,&raw,32)==32);
        t=start(&req);
        assert(write(writer,(const char*)&raw+32,16)==16);
        join(t);
        assert(req.n==1 && req.result.type==list[i].type && req.result.sensor==list[i].handle);
        assert(req.result.timestamp==raw.motion.timestamp);
        assert(req.result.data[0]==1.5f && req.result.data[1]==-2.5f && req.result.data[2]==3.5f);
        assert(req.result.data[3]==0.25f && req.result.data[4]==-0.5f && req.result.data[5]==0.75f);
    }
    /* A full wake pipe is already readable, so EAGAIN must not reject flush. */
    char byte=1;
    while (write(d->wake_write,&byte,1)==1) {}
    assert(errno==EAGAIN);
    assert(dev_flush(d,HANDLE_GYROSCOPE)==0);
    t=start(&req); join(t); assert(is_flush(&req.result,HANDLE_GYROSCOPE));
    /* A flush must wake a poll blocked without incoming samples. */
    t=start(&req); usleep(30000); wake_errno=EINTR; wake_faults=1; assert(dev_flush(d,1)==0); join(t); assert(is_flush(&req.result,HANDLE_ACCEL));
    /* Disabling and reenabling must discard samples from the previous subscription. */
    assert(dev_activate(d,1,0)==0); p.timestamp=now(); p.x=123; put(writer,&p);
    usleep(10000); assert(dev_activate(d,1,1)==0);
    usleep(25000); p.timestamp=now(); p.x=7; put(writer,&p);
    t=start(&req); join(t); assert(req.result.type==1 && req.result.data[0]==7);
    /* Samples already in the FIFO precede flush complete, even without another host write. */
    usleep(25000); p.timestamp=now(); p.x=8; put(writer,&p);
    assert(dev_flush(d,1)==0);
    t=start(&req); join(t); assert(req.result.type==1 && req.result.data[0]==8);
    t=start(&req); join(t); assert(is_flush(&req.result,HANDLE_ACCEL));
    /* A paused host invalidates queued frames even if guest subscription stays active. */
    usleep(25000); p.timestamp=now(); p.x=99; put(writer,&p);
    state(0,now()); usleep(10000); state(1,now());
    usleep(25000); p.timestamp=now(); p.x=11; put(writer,&p);
    t=start(&req); join(t); assert(req.result.type==1 && req.result.data[0]==11);
    /* Byte-stream resynchronization skips corruption, stale timestamps and duplicates. */
    int64_t delivered=p.timestamp;
    usleep(25000); assert(write(writer,"bad",3)==3);
    p.timestamp=delivered; p.x=20; put(writer,&p);
    p.timestamp=now()-600000000LL; p.x=21; put(writer,&p);
    p.timestamp=now()+50000000LL; p.x=22; put(writer,&p);
    p.timestamp=now(); p.x=23; put(writer,&p);
    t=start(&req); join(t); assert(req.result.type==1 && req.result.data[0]==23);
    /* A continuous 50 Hz stream cannot starve a flush. */
    struct stream stream={.fd=writer}; pthread_t producer; assert(!pthread_create(&producer,NULL,produce,&stream));
    usleep(80000); assert(dev_flush(d,1)==0);
    int saw_flush=0;
    for (int i=0;i<8 && !saw_flush;i++) { t=start(&req); join(t); saw_flush=is_flush(&req.result,HANDLE_ACCEL); }
    assert(saw_flush); stream.stop=1; assert(!pthread_join(producer,NULL));
    /* A flush accepted while active must complete after disable as well. */
    assert(dev_flush(d,1)==0); assert(dev_activate(d,1,0)==0);
    t=start(&req); join(t); assert(is_flush(&req.result,HANDLE_ACCEL));
    assert(dev_flush(d,1)==-EINVAL);
    /* Closing the service while poll waits must wake and join the poller safely. */
    t=start(&req); usleep(30000);
    struct request concurrent={.device=d}; pthread_t second=start(&concurrent);
    join(second); assert(concurrent.n==-EBUSY);
    /* Failure to wake does not free the device under the blocked poller. */
    wake_errno=EBADF; wake_faults=1; assert(dev_close(base)==-EBADF);
    wake_errno=EINTR; wake_faults=1;
    close(writer); assert(dev_close(base)==0); join(t); assert(req.n==-ECANCELED);
    unlink(AEMU_SENSORS_FIFO_PATH); unlink(AEMU_SENSORS_STATE_PATH);
    puts("N64 HAL FIFO, activation, timestamp, accuracy and flush contracts passed");
#endif
    return 0;
}
