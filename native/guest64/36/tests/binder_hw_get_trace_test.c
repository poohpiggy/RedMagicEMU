#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../binder_hw_get_trace.h"

#define PTR_TYPE 0x70742a85u
#define DATA_SIZE 208u
#define OFF_SIZE 32u
#define ROOT_SIZE 16u
struct obj { uint32_t type, flags; uint64_t buffer, length, parent, parent_offset; };
_Static_assert(sizeof(struct obj) == 40, "arm64 binder object layout");
static unsigned char payload[512], original[512];
static size_t extra_size, received;
static const char c2[] = "android.hardware.media.c2@1.0::IComponentStore";
static const char omx[] = "android.hardware.media.omx@1.0::IOmxStore";
static size_t a8(size_t n) { return (n + 7) & ~(size_t)7; }
static void put32(size_t at, uint32_t v) { memcpy(payload + at, &v, sizeof(v)); }
static void put64(size_t at, uint64_t v) { memcpy(payload + at, &v, sizeof(v)); }
static void putobj(size_t at, unsigned flags, size_t len, size_t parent, size_t parent_offset) {
    struct obj o = { PTR_TYPE, flags, 0, len, parent, parent_offset };
    memcpy(payload + at, &o, sizeof(o));
}
static void setup(const char *service, const char *instance) {
    static const char token[] = "android.hidl.manager@1.0::IServiceManager";
    size_t sl = strlen(service), il = strlen(instance), sg = DATA_SIZE + OFF_SIZE;
    memset(payload, 0, sizeof payload);
    memcpy(payload, token, sizeof token);
    putobj(48, 0, ROOT_SIZE, 0, 0); putobj(88, 1, sl + 1, 0, 0);
    putobj(128, 0, ROOT_SIZE, 0, 0); putobj(168, 1, il + 1, 2, 0);
    for (size_t i = 0; i < 4; i++) put64(DATA_SIZE + i * 8, 48 + i * 40);
    put32(sg + 8, (uint32_t)sl); memcpy(payload + sg + ROOT_SIZE, service, sl + 1);
    size_t second = sg + ROOT_SIZE + a8(sl + 1);
    put32(second + 8, (uint32_t)il); memcpy(payload + second + ROOT_SIZE, instance, il + 1);
    extra_size = ROOT_SIZE + a8(sl + 1) + ROOT_SIZE + a8(il + 1);
    received = sg + extra_size;
    assert(received <= sizeof(payload)); memcpy(original, payload, sizeof payload);
}
static struct hwget_class classify(int enabled, int safe, const void *p, size_t r, size_t d, size_t o, size_t x) {
    return hwget_classify(enabled, safe, p, r, d, o, x);
}
static void unavailable(struct hwget_class c) { assert(c.service == HWGET_UNAVAILABLE && c.instance == HWINST_UNAVAILABLE); }
static void valid(int service, int instance) {
    struct hwget_class c = classify(1,1,payload,received,DATA_SIZE,OFF_SIZE,extra_size);
    assert(c.service == service && c.instance == instance);
}
static void bad(void) { unavailable(classify(1,1,payload,received,DATA_SIZE,OFF_SIZE,extra_size)); memcpy(payload,original,sizeof payload); }
int main(void) {
    unavailable(classify(0,1,(void*)1,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX));
    unavailable(classify(1,0,(void*)1,SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX));
    unavailable(classify(1,1,NULL,0,0,0,0));
    setup(c2,"default"); valid(HWGET_C2,HWINST_DEFAULT);
    for(size_t n=0;n<received;n++) unavailable(classify(1,1,payload,n,DATA_SIZE,OFF_SIZE,extra_size));
    unavailable(classify(1,1,payload,received,SIZE_MAX,OFF_SIZE,extra_size));
    unavailable(classify(1,1,payload,received,DATA_SIZE,SIZE_MAX,extra_size));
    unavailable(classify(1,1,payload,received,DATA_SIZE,OFF_SIZE,SIZE_MAX));
    unavailable(classify(1,1,payload,received,DATA_SIZE,OFF_SIZE-8,extra_size));
    unavailable(classify(1,1,payload,received,DATA_SIZE,OFF_SIZE+8,extra_size));
    payload[0]^=1; {struct hwget_class c=classify(1,1,payload,received,DATA_SIZE,OFF_SIZE,extra_size); assert(c.service==HWGET_NOT_MANAGER && c.instance==HWINST_UNAVAILABLE);} memcpy(payload,original,sizeof payload); payload[41]='X'; bad();
    payload[3]=0; bad();
    for (size_t i=0;i<4;i++) {
        put64(DATA_SIZE+i*8, DATA_SIZE-1); bad();
        put64(DATA_SIZE+i*8, UINT64_MAX); bad();
        put32(48+i*40, 0); bad();
    }
    put64(DATA_SIZE+8,48); bad();
    put32(48+4,1); bad(); put32(88+4,0); bad(); put32(88+4,3); bad();
    put32(128+4,1); bad(); put32(168+4,0); bad();
    put64(88+24,2); bad(); put64(168+24,0); bad();
    put64(88+32,8); bad(); put64(168+32,8); bad();
    put64(48+16,15); bad(); put64(128+16,UINT64_MAX); bad();
    put64(88+16,UINT64_MAX); bad(); put64(168+16,0); bad();
    put32(DATA_SIZE+OFF_SIZE+8,UINT32_MAX); bad();
    payload[DATA_SIZE+OFF_SIZE+ROOT_SIZE+strlen(c2)]='X'; bad();
    payload[DATA_SIZE+OFF_SIZE+ROOT_SIZE+1]=0; bad();
    {size_t p=DATA_SIZE+OFF_SIZE+ROOT_SIZE+a8(strlen(c2)+1); put32(p+8,UINT32_MAX); bad(); payload[p+ROOT_SIZE+strlen("default")]='X'; bad();}
    setup(omx,"default"); valid(HWGET_OMX,HWINST_DEFAULT);
    setup("different.service","other"); valid(HWGET_OTHER,HWINST_OTHER);
    setup(c2,"other"); valid(HWGET_C2,HWINST_OTHER);
    return 0;
}
