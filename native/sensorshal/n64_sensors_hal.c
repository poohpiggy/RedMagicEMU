/*
 * N64/API 36 AEmulator motion-sensor HAL, derived from sensors_hal.c in this
 * directory (AEmulator); the legacy ARMv7 HAL remains unchanged.
 *
 * AEM2 records contain the type, boot-time ns, vector and accuracy. AEM3
 * records carry an uncalibrated vector and its bias in one atomic FIFO write.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef AEMU_SENSORS_FIFO_PATH
#define AEMU_SENSORS_FIFO_PATH "/dev/aemu_sensors"
#endif
#ifndef AEMU_SENSORS_STATE_PATH
#define AEMU_SENSORS_STATE_PATH "/dev/aemu_sensor_state"
#endif
#ifndef AEMU_SENSORS_CAPABILITIES_PATH
#define AEMU_SENSORS_CAPABILITIES_PATH "/dev/.aemu-sensor-caps"
#endif
#define EXPORT __attribute__((visibility("default")))
#define HANDLE_ACCEL 1
#define HANDLE_GYROSCOPE 2
#define HANDLE_MAGNETIC_FIELD 3
#define HANDLE_GYROSCOPE_UNCALIBRATED 4
#define HANDLE_MAGNETIC_FIELD_UNCALIBRATED 5
#define SENSOR_COUNT 5
#define BASE_SENSOR_COUNT 3
#define AEM2_MAGIC 0x324d4541u
#define AEM3_MAGIC 0x334d4541u
/* The reporting mode is encoded in bits 1-3. Continuous is zero; bit 0 is
 * SENSOR_FLAG_WAKE_UP and must stay clear for the framework's non-wake-up
 * fusion sensors. */
#define SENSOR_FLAG_CONTINUOUS_MODE 0x0u
#define SENSOR_FLAG_ON_CHANGE_MODE 0x2u
#define MIN_PERIOD_NS 20000000LL
#define MAX_PERIOD_NS 1000000000LL
#define MAX_AGE_NS 500000000LL
#define MAX_FUTURE_NS 5000000LL
#define MAX_FLUSH 64

/* libhardware ABI for LP64; matching fields in hardware.h and sensors.h. */
struct hw_module;
struct hw_device;
struct hw_module_methods { int (*open)(const struct hw_module *, const char *, struct hw_device **); };
struct hw_module {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct hw_module_methods *methods;
    void *dso;
    uint64_t reserved[25];
};
struct hw_device {
    uint32_t tag, version;
    struct hw_module *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device *);
};
struct sensor_t {
    const char *name, *vendor;
    int version, handle, type;
    float maxRange, resolution, power;
    int32_t minDelay;
    uint32_t fifoReservedEventCount, fifoMaxEventCount;
    const char *stringType, *requiredPermission;
    long maxDelay;
    unsigned long flags;
    void *reserved[2];
};
struct sensors_event_t {
    int32_t version, sensor, type, reserved0;
    int64_t timestamp;
    float data[16];
    uint32_t flags, reserved1[3];
};
struct sensors_module {
    struct hw_module common;
    int (*get_sensors_list)(struct sensors_module *, const struct sensor_t **);
    void (*set_operation_mode)(unsigned);
};
struct packet { uint32_t magic; int32_t type; int64_t timestamp; float x,y,z; int32_t accuracy; };
struct uncalibrated_packet { struct packet motion; float bias[3]; uint32_t reserved; };
_Static_assert(sizeof(struct packet) == 32, "AEM2 protocol layout");
_Static_assert(sizeof(struct uncalibrated_packet) == 48, "AEM3 protocol layout");
_Static_assert(sizeof(struct sensors_event_t) == 104, "sensors_event_t ABI");
struct poll_device {
    struct hw_device common;
    int (*activate)(struct poll_device *, int, int);
    int (*setDelay)(struct poll_device *, int, int64_t);
    int (*poll)(struct poll_device *, struct sensors_event_t *, int);
    int (*batch)(struct poll_device *, int, int, int64_t, int64_t);
    int (*flush)(struct poll_device *, int);
    void *reserved[8];
    int fd, wake_read, wake_write;
    pthread_mutex_t lock;
    pthread_cond_t idle;
    unsigned pollers;
    int closing;
    int active;
    int64_t enabled_since[SENSOR_COUNT], last_event[SENSOR_COUNT], period[SENSOR_COUNT], next_sample[SENSOR_COUNT];
    uint8_t bytes[sizeof(struct uncalibrated_packet)];
    size_t used;
    uint64_t bytes_read;
    uint64_t flush_boundary[MAX_FLUSH];
    uint8_t flush_handle[MAX_FLUSH];
    unsigned flush_head, flush_count;
};

_Static_assert(sizeof(struct hw_module) == 248, "LP64 hw_module ABI");
_Static_assert(sizeof(struct hw_device) == 120, "LP64 hw_device ABI");
_Static_assert(sizeof(struct sensor_t) == 104, "LP64 sensor_t ABI");
_Static_assert(offsetof(struct poll_device, activate) == 120, "LP64 activate ABI");
_Static_assert(offsetof(struct poll_device, poll) == 136, "LP64 poll ABI");
_Static_assert(offsetof(struct poll_device, batch) == 144, "LP64 batch ABI");
_Static_assert(offsetof(struct poll_device, flush) == 152, "LP64 flush ABI");

static const struct sensor_t sensors[] = {
    {
        .name="AEmulator 3-axis Accelerometer", .vendor="AEmulator", .version=1,
        .handle=HANDLE_ACCEL, .type=1, .maxRange=39.2265625f, .resolution=1.0f/4096.0f,
        .power=0.2f, .minDelay=20000, .stringType="android.sensor.accelerometer", .requiredPermission="",
        .maxDelay=1000000, .flags=SENSOR_FLAG_CONTINUOUS_MODE,
    },
    {
        .name="AEmulator 3-axis Gyroscope", .vendor="AEmulator", .version=1,
        .handle=HANDLE_GYROSCOPE, .type=4, .maxRange=32.0f, .resolution=1.0f/1024.0f,
        .power=0.2f, .minDelay=20000, .stringType="android.sensor.gyroscope", .requiredPermission="",
        .maxDelay=1000000, .flags=SENSOR_FLAG_CONTINUOUS_MODE,
    },
    {
        .name="AEmulator 3-axis Magnetometer", .vendor="AEmulator", .version=1,
        .handle=HANDLE_MAGNETIC_FIELD, .type=2, .maxRange=2000.0f, .resolution=0.125f,
        .power=0.15f, .minDelay=20000, .stringType="android.sensor.magnetic_field", .requiredPermission="",
        .maxDelay=1000000, .flags=SENSOR_FLAG_CONTINUOUS_MODE,
    },
    {
        .name="AEmulator 3-axis Gyroscope", .vendor="AEmulator", .version=1,
        .handle=HANDLE_GYROSCOPE_UNCALIBRATED, .type=16, .maxRange=32.0f, .resolution=1.0f/1024.0f,
        .power=0.2f, .minDelay=20000, .stringType="android.sensor.gyroscope_uncalibrated", .requiredPermission="",
        .maxDelay=1000000, .flags=SENSOR_FLAG_CONTINUOUS_MODE,
    },
    {
        .name="AEmulator 3-axis Magnetometer", .vendor="AEmulator", .version=1,
        .handle=HANDLE_MAGNETIC_FIELD_UNCALIBRATED, .type=14, .maxRange=2000.0f, .resolution=0.125f,
        .power=0.15f, .minDelay=20000, .stringType="android.sensor.magnetic_field_uncalibrated", .requiredPermission="",
        .maxDelay=1000000, .flags=SENSOR_FLAG_CONTINUOUS_MODE,
    },
};
static struct sensor_t available_sensors[SENSOR_COUNT];
static int available_count;
static pthread_once_t available_once=PTHREAD_ONCE_INIT;

static int read_capabilities(void) {
    char record[3];
    int fd=open(AEMU_SENSORS_CAPABILITIES_PATH,O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);
    if (fd<0) return 0;
    ssize_t n=read(fd,record,sizeof(record));
    close(fd);
    if (n!=2 || record[0]<'0' || record[0]>'3' || record[1]!='\n') return 0;
    return record[0]-'0';
}

static void load_sensor_list(void) {
    int mask=read_capabilities();
    memcpy(available_sensors,sensors,BASE_SENSOR_COUNT*sizeof(sensors[0]));
    available_count=BASE_SENSOR_COUNT;
    for (int i=BASE_SENSOR_COUNT;i<SENSOR_COUNT;i++) {
        if (mask & (1<<(i-BASE_SENSOR_COUNT))) available_sensors[available_count++]=sensors[i];
    }
}

static int get_list(struct sensors_module *m, const struct sensor_t **list) {
    (void)m;
    pthread_once(&available_once,load_sensor_list);
    *list=available_sensors;
    return available_count;
}

static int handle_index(int handle) {
    return handle>=1 && handle<=SENSOR_COUNT ? handle-1 : -1;
}
static int type_handle(int type) {
    switch (type) {
        case 1: return HANDLE_ACCEL;
        case 4: return HANDLE_GYROSCOPE;
        case 2: return HANDLE_MAGNETIC_FIELD;
        case 16: return HANDLE_GYROSCOPE_UNCALIBRATED;
        case 14: return HANDLE_MAGNETIC_FIELD_UNCALIBRATED;
        default: return -1;
    }
}
static int64_t now_ns(void) { struct timespec t; if (clock_gettime(CLOCK_BOOTTIME,&t)) return -1; return (int64_t)t.tv_sec*1000000000LL+t.tv_nsec; }
static int wake_poll(struct poll_device *d) {
    char c=1;
    ssize_t n;
    do { n=write(d->wake_write,&c,1); } while (n<0 && errno==EINTR);
    /* EAGAIN means the nonblocking wake pipe is full and already readable. */
    if (n==1 || (n<0 && errno==EAGAIN)) return 0;
    return n<0 ? -errno : -EIO;
}
static int dev_activate(struct poll_device *d, int handle, int enabled) {
    int index=handle_index(handle);
    if (index<0 || (enabled!=0 && enabled!=1)) return -EINVAL;
    int64_t t=enabled ? now_ns() : 0;
    if (enabled && t<0) return -errno;
    pthread_mutex_lock(&d->lock);
    if (d->closing) { pthread_mutex_unlock(&d->lock); return -ECANCELED; }
    int old_active=d->active;
    int bit=1<<index;
    int64_t old_since=d->enabled_since[index], old_next=d->next_sample[index];
    if (enabled && !(d->active&bit)) { d->enabled_since[index]=t; d->next_sample[index]=0; }
    if (enabled) d->active|=bit; else d->active&=~bit;
    int err=wake_poll(d);
    if (err) { d->active=old_active; d->enabled_since[index]=old_since; d->next_sample[index]=old_next; }
    pthread_mutex_unlock(&d->lock);
    return err;
}
static int dev_batch(struct poll_device *d, int handle, int flags, int64_t period, int64_t latency) {
    int index=handle_index(handle);
    if (index<0 || flags!=0 || period<0 || latency<0) return -EINVAL;
    if (period<MIN_PERIOD_NS) period=MIN_PERIOD_NS;
    if (period>MAX_PERIOD_NS) period=MAX_PERIOD_NS;
    pthread_mutex_lock(&d->lock);
    if (d->closing) { pthread_mutex_unlock(&d->lock); return -ECANCELED; }
    d->period[index]=period;
    d->next_sample[index]=d->last_event[index] ? d->last_event[index]+period : 0;
    pthread_mutex_unlock(&d->lock);
    return 0;
}
static int dev_set_delay(struct poll_device *d, int handle, int64_t period) { return dev_batch(d,handle,0,period,0); }
static int dev_flush(struct poll_device *d, int handle) {
    int index=handle_index(handle);
    if (index<0) return -EINVAL;
    pthread_mutex_lock(&d->lock);
    if (d->closing) { pthread_mutex_unlock(&d->lock); return -ECANCELED; }
    if (!(d->active&(1<<index))) { pthread_mutex_unlock(&d->lock); return -EINVAL; }
    if (d->flush_count==MAX_FLUSH) { pthread_mutex_unlock(&d->lock); return -ENOMEM; }
    int pending=0;
    if (ioctl(d->fd,FIONREAD,&pending)<0) { int err=errno; pthread_mutex_unlock(&d->lock); return -err; }
    unsigned slot=(d->flush_head+d->flush_count)%MAX_FLUSH;
    d->flush_boundary[slot]=d->bytes_read+(uint64_t)pending;
    d->flush_handle[slot]=(uint8_t)handle;
    d->flush_count++;
    int err=wake_poll(d);
    if (err) d->flush_count--;
    pthread_mutex_unlock(&d->lock);
    return err;
}
/* Atomically replaced by the host on pause/resume. Missing or malformed state
 * fails closed; samples from before the current host subscription are stale. */
static int host_accepts(int64_t timestamp) {
    char buf[64];
    int fd=open(AEMU_SENSORS_STATE_PATH,O_RDONLY|O_CLOEXEC);
    if (fd<0) return 0;
    ssize_t n=read(fd,buf,sizeof(buf)-1); close(fd);
    if (n<4 || n>=(ssize_t)sizeof(buf)-1) return 0;
    buf[n]=0;
    if (buf[0]!='1' || buf[1]!=' ') return 0;
    char *end; errno=0;
    long long earliest=strtoll(buf+2,&end,10);
    if (errno || end==buf+2 || earliest<0 || strcmp(end,"\n")) return 0;
    return timestamp>=earliest;
}
static int take_sample(struct poll_device *d, struct sensors_event_t *out) {
    struct packet p; memcpy(&p,d->bytes,sizeof(p));
    int handle=type_handle(p.type), index=handle_index(handle);
    int uncalibrated=handle==HANDLE_GYROSCOPE_UNCALIBRATED || handle==HANDLE_MAGNETIC_FIELD_UNCALIBRATED;
    if (p.magic!=(uncalibrated ? AEM3_MAGIC : AEM2_MAGIC) || index<0 ||
        (!uncalibrated && (p.accuracy<0 || p.accuracy>3)) ||
        !isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)) return 0;
    float bias[3];
    if (uncalibrated) {
        memcpy(bias,d->bytes+sizeof(p),sizeof(bias));
        if (!isfinite(bias[0]) || !isfinite(bias[1]) || !isfinite(bias[2])) return 0;
    }
    int64_t now=now_ns();
    if (now<0 || !(d->active&(1<<index)) || p.timestamp<d->enabled_since[index] ||
        p.timestamp<=d->last_event[index] ||
        p.timestamp>now+MAX_FUTURE_NS || p.timestamp<now-MAX_AGE_NS || !host_accepts(p.timestamp) ||
        (d->next_sample[index] && p.timestamp<d->next_sample[index])) return 0;
    memset(out,0,sizeof(*out)); out->version=sizeof(*out); out->sensor=handle; out->type=p.type;
    out->timestamp=p.timestamp; out->data[0]=p.x; out->data[1]=p.y; out->data[2]=p.z;
    if (uncalibrated) memcpy(out->data+3,bias,sizeof(bias));
    else ((int8_t *)&out->data[3])[0]=(int8_t)p.accuracy;
    if (!d->next_sample[index]) d->next_sample[index]=p.timestamp+d->period[index];
    else d->next_sample[index]+=((p.timestamp-d->next_sample[index])/d->period[index]+1)*d->period[index];
    d->last_event[index]=p.timestamp;
    return 1;
}

static uint32_t packet_magic(const struct poll_device *d) {
    uint32_t magic=0;
    if (d->used>=sizeof(magic)) memcpy(&magic,d->bytes,sizeof(magic));
    return magic;
}

static size_t packet_size(uint32_t magic) {
    return magic==AEM3_MAGIC ? sizeof(struct uncalibrated_packet) : sizeof(struct packet);
}
static int finish_poll(struct poll_device *d, int result) {
    pthread_mutex_lock(&d->lock);
    d->pollers--;
    if (d->closing && !d->pollers) pthread_cond_signal(&d->idle);
    pthread_mutex_unlock(&d->lock);
    return result;
}
static int dev_poll(struct poll_device *d, struct sensors_event_t *out, int count) {
    if (count<=0 || !out) return -EINVAL;
    pthread_mutex_lock(&d->lock);
    if (d->closing) { pthread_mutex_unlock(&d->lock); return -ECANCELED; }
    if (d->pollers) { pthread_mutex_unlock(&d->lock); return -EBUSY; }
    d->pollers++;
    pthread_mutex_unlock(&d->lock);
    for (;;) {
        pthread_mutex_lock(&d->lock);
        if (d->closing) { pthread_mutex_unlock(&d->lock); return finish_poll(d,-ECANCELED); }
        if (d->flush_count && d->bytes_read>=d->flush_boundary[d->flush_head]) {
            /* A malformed partial frame at the fence must never block flush forever. */
            int handle=d->flush_handle[d->flush_head];
            d->used=0; d->flush_head=(d->flush_head+1)%MAX_FLUSH; d->flush_count--;
            memset(out,0,sizeof(*out)); out->version=2; out->type=0;
            ((int32_t *)out->data)[0]=1; ((int32_t *)out->data)[1]=handle;
            pthread_mutex_unlock(&d->lock); return finish_poll(d,1);
        }
        size_t need=packet_size(packet_magic(d))-d->used;
        if (d->flush_count) {
            uint64_t left=d->flush_boundary[d->flush_head]-d->bytes_read;
            if (left<need) need=(size_t)left;
        }
        ssize_t n=read(d->fd,d->bytes+d->used,need);
        if (n>0) {
            d->used+=(size_t)n; d->bytes_read+=(uint64_t)n;
            uint32_t magic=packet_magic(d);
            if (d->used==packet_size(magic)) {
                if (magic!=AEM2_MAGIC && magic!=AEM3_MAGIC) { memmove(d->bytes,d->bytes+1,--d->used); }
                else { d->used=0; if (take_sample(d,out)) { pthread_mutex_unlock(&d->lock); return finish_poll(d,1); } }
            }
            pthread_mutex_unlock(&d->lock); continue;
        }
        int err=errno;
        pthread_mutex_unlock(&d->lock);
        if (n<0 && err!=EAGAIN && err!=EINTR) return finish_poll(d,-err);
        struct pollfd fds[2]={{.fd=d->fd,.events=POLLIN},{.fd=d->wake_read,.events=POLLIN}};
        int ready; do { ready=poll(fds,2,-1); } while (ready<0 && errno==EINTR);
        if (ready<0) return finish_poll(d,-errno);
        if (fds[1].revents&POLLIN) { char buffer[64]; while (read(d->wake_read,buffer,sizeof(buffer))>0) {} }
        if ((fds[0].revents|fds[1].revents)&(POLLERR|POLLNVAL)) return finish_poll(d,-EIO);
    }
}
static int dev_close(struct hw_device *h) {
    struct poll_device *d=(struct poll_device *)h;
    pthread_mutex_lock(&d->lock);
    d->closing=1;
    if (d->pollers) {
        int err=wake_poll(d);
        if (err) { d->closing=0; pthread_mutex_unlock(&d->lock); return err; }
    }
    while (d->pollers) {
        int err=pthread_cond_wait(&d->idle,&d->lock);
        if (err) { pthread_mutex_unlock(&d->lock); return -err; }
    }
    pthread_mutex_unlock(&d->lock);
    close(d->fd); close(d->wake_read); close(d->wake_write);
    pthread_cond_destroy(&d->idle); pthread_mutex_destroy(&d->lock); free(d); return 0;
}
static int sensors_open(const struct hw_module *m, const char *id, struct hw_device **out) {
    if (!out || !id || strcmp(id,"poll")) return -EINVAL;
    struct poll_device *d=calloc(1,sizeof(*d)); if (!d) return -ENOMEM;
    d->fd=open(AEMU_SENSORS_FIFO_PATH,O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if (d->fd<0) { int err=errno; free(d); return -err; }
    int fds[2];
    if (pipe2(fds,O_NONBLOCK|O_CLOEXEC)) { int err=errno; close(d->fd); free(d); return -err; }
    d->wake_read=fds[0]; d->wake_write=fds[1];
    int err=pthread_mutex_init(&d->lock,NULL);
    if (err) { close(d->fd); close(fds[0]); close(fds[1]); free(d); return -err; }
    err=pthread_cond_init(&d->idle,NULL);
    if (err) { pthread_mutex_destroy(&d->lock); close(d->fd); close(fds[0]); close(fds[1]); free(d); return -err; }
    d->common.tag=0x48574454; d->common.version=0x01030001; d->common.module=(struct hw_module *)m; d->common.close=dev_close;
    d->activate=dev_activate; d->setDelay=dev_set_delay; d->poll=dev_poll; d->batch=dev_batch; d->flush=dev_flush;
    for (int i=0;i<SENSOR_COUNT;i++) d->period[i]=MIN_PERIOD_NS;
    *out=&d->common; return 0;
}
static struct hw_module_methods methods={sensors_open};
EXPORT struct sensors_module HMI={.common={.tag=0x48574d54,.module_api_version=1,.hal_api_version=0,
    .id="sensors",.name="AEmulator sensors",.author="AEmulator",.methods=&methods},.get_sensors_list=get_list};
